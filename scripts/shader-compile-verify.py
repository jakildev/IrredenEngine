#!/usr/bin/env python3
"""Cold-compile every top-level OpenGL shader program and time compile + link.

NVIDIA's GL driver defers most codegen into glLinkProgram and caches the
result on disk per executable, so a slow program only shows the cost the first
time an executable sees its exact source. Each run inserts a unique nonce line
after ``#version`` so every program misses that cache and the reading is the
cold cost a fresh install pays.

Programs are the ``.glsl`` files directly in the shader directory that are not
``ir_*`` includes or ``*_body.glsl`` fragments; the stage comes from the
``c_`` / ``v_`` / ``f_`` prefix. Includes resolve as the engine's
``detail::resolveShaderIncludes`` does: the first include of a canonical path
wins and later ones are dropped. Each program links alone.

Native Windows only (WGL through ctypes). Measure with no other engine GL
context live: a second context stalls every link on this driver and fakes a
slow reading.

Usage:
  shader-compile-verify.py [--shader-dir DIR] [--budget SECONDS] [FILE ...]

Exit codes:
  0 — every program compiled and linked within the budget
  1 — a program exceeded the budget or failed to compile or link
  2 — usage error, unsupported host, or no GL context
"""

from __future__ import annotations

import argparse
import os
import sys
import time
import uuid
from pathlib import Path

DEFAULT_SHADER_DIR = Path(__file__).resolve().parent.parent / "engine/render/src/shaders"
DEFAULT_BUDGET_S = 5.0

STAGE_BY_PREFIX = {"c_": "comp", "v_": "vert", "f_": "frag"}

GL_COMPUTE_SHADER = 0x91B9
GL_VERTEX_SHADER = 0x8B31
GL_FRAGMENT_SHADER = 0x8B30
GL_COMPILE_STATUS = 0x8B81
GL_LINK_STATUS = 0x8B82
GL_RENDERER = 0x1F01
GL_VERSION = 0x1F02
GL_STAGE_ENUM = {"comp": GL_COMPUTE_SHADER, "vert": GL_VERTEX_SHADER, "frag": GL_FRAGMENT_SHADER}


def stage_for(name: str) -> str | None:
    for prefix, stage in STAGE_BY_PREFIX.items():
        if name.startswith(prefix):
            return stage
    return None


def select_programs(shader_dir: Path) -> list[Path]:
    return sorted(
        path for path in shader_dir.glob("*.glsl")
        if not path.name.startswith("ir_") and not path.name.endswith("_body.glsl")
    )


def _read_source(path: Path) -> str:
    with open(path, encoding="utf-8", newline="") as handle:
        return handle.read()


def _canonical(path: Path) -> str:
    return os.path.realpath(path)


def _resolve(source: str, base_dir: Path, visited: set[str]) -> str:
    lines = source.split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    out = []
    for line in lines:
        trimmed = line.lstrip(" \t")
        if trimmed.startswith('#include "'):
            close = trimmed.find('"', 10)
            if close != -1:
                include = base_dir / trimmed[10:close]
                canonical = _canonical(include)
                if canonical not in visited:
                    visited.add(canonical)
                    out.append(_resolve(_read_source(include), include.parent, visited) + "\n")
                continue
        out.append(line + "\n")
    return "".join(out)


def resolve_includes(path: Path) -> str:
    return _resolve(_read_source(path), path.parent, {_canonical(path)})


def inject_nonce(source: str, nonce: str) -> str:
    lines = source.split("\n")
    for index, line in enumerate(lines):
        if line.startswith("#version"):
            lines.insert(index + 1, f"const float _nonce_{nonce} = 0.0;")
            return "\n".join(lines)
    raise ValueError("no #version line")


class _GLContext:
    """A hidden window's legacy WGL context; the driver serves its newest profile."""

    def __init__(self) -> None:
        import ctypes
        import ctypes.wintypes as wt

        self._ctypes = ctypes
        user32 = ctypes.windll.user32
        gdi32 = ctypes.windll.gdi32
        gl = ctypes.windll.opengl32
        user32.CreateWindowExW.restype = wt.HWND
        user32.CreateWindowExW.argtypes = [
            wt.DWORD, wt.LPCWSTR, wt.LPCWSTR, wt.DWORD, ctypes.c_int, ctypes.c_int,
            ctypes.c_int, ctypes.c_int, wt.HWND, wt.HMENU, wt.HINSTANCE, wt.LPVOID,
        ]
        user32.GetDC.restype = wt.HDC
        user32.GetDC.argtypes = [wt.HWND]
        hwnd = user32.CreateWindowExW(0, "STATIC", "shader-compile-verify", 0, 0, 0, 16, 16,
                                      None, None, None, None)
        if not hwnd:
            raise RuntimeError("CreateWindowExW failed")
        hdc = user32.GetDC(hwnd)

        class PixelFormatDescriptor(ctypes.Structure):
            _fields_ = (
                [("nSize", wt.WORD), ("nVersion", wt.WORD), ("dwFlags", wt.DWORD),
                 ("iPixelType", ctypes.c_ubyte), ("cColorBits", ctypes.c_ubyte)]
                + [(f"unused{i}", ctypes.c_ubyte) for i in range(13)]
                + [("cDepthBits", ctypes.c_ubyte), ("cStencilBits", ctypes.c_ubyte),
                   ("cAuxBuffers", ctypes.c_ubyte), ("iLayerType", ctypes.c_ubyte),
                   ("bReserved", ctypes.c_ubyte), ("dwLayerMask", wt.DWORD),
                   ("dwVisibleMask", wt.DWORD), ("dwDamageMask", wt.DWORD)]
            )

        pfd = PixelFormatDescriptor()
        pfd.nSize = ctypes.sizeof(PixelFormatDescriptor)
        pfd.nVersion = 1
        pfd.dwFlags = 0x4 | 0x20 | 0x1  # DRAW_TO_WINDOW | SUPPORT_OPENGL | DOUBLEBUFFER
        pfd.cColorBits = 32
        pfd.cDepthBits = 24
        pfd_pointer = ctypes.POINTER(PixelFormatDescriptor)
        gdi32.ChoosePixelFormat.argtypes = [wt.HDC, pfd_pointer]
        gdi32.SetPixelFormat.argtypes = [wt.HDC, ctypes.c_int, pfd_pointer]
        pixel_format = gdi32.ChoosePixelFormat(hdc, ctypes.byref(pfd))
        if not pixel_format or not gdi32.SetPixelFormat(hdc, pixel_format, ctypes.byref(pfd)):
            raise RuntimeError("no OpenGL pixel format")
        gl.wglCreateContext.restype = wt.HANDLE
        gl.wglCreateContext.argtypes = [wt.HDC]
        gl.wglMakeCurrent.argtypes = [wt.HDC, wt.HANDLE]
        context = gl.wglCreateContext(hdc)
        if not context or not gl.wglMakeCurrent(hdc, context):
            raise RuntimeError("wglCreateContext failed")
        gl.glGetString.restype = ctypes.c_char_p
        gl.glGetString.argtypes = [ctypes.c_uint]
        gl.wglGetProcAddress.restype = ctypes.c_void_p
        gl.wglGetProcAddress.argtypes = [ctypes.c_char_p]
        self.renderer = gl.glGetString(GL_RENDERER).decode(errors="replace")
        self.version = gl.glGetString(GL_VERSION).decode(errors="replace")

        u, i, p = ctypes.c_uint, ctypes.c_int, ctypes.c_void_p

        def entry(name, result, *args):
            address = gl.wglGetProcAddress(name.encode())
            if not address:
                raise RuntimeError(f"driver exposes no {name}")
            return ctypes.WINFUNCTYPE(result, *args)(address)

        self.create_shader = entry("glCreateShader", u, u)
        self.shader_source = entry("glShaderSource", None, u, i, ctypes.POINTER(ctypes.c_char_p), p)
        self.compile_shader = entry("glCompileShader", None, u)
        self.get_shader_iv = entry("glGetShaderiv", None, u, u, ctypes.POINTER(i))
        self.get_shader_log = entry("glGetShaderInfoLog", None, u, i, p, ctypes.c_char_p)
        self.delete_shader = entry("glDeleteShader", None, u)
        self.create_program = entry("glCreateProgram", u)
        self.attach_shader = entry("glAttachShader", None, u, u)
        self.link_program = entry("glLinkProgram", None, u)
        self.get_program_iv = entry("glGetProgramiv", None, u, u, ctypes.POINTER(i))
        self.get_program_log = entry("glGetProgramInfoLog", None, u, i, p, ctypes.c_char_p)
        self.delete_program = entry("glDeleteProgram", None, u)

    def compile_and_link(self, source: str, stage: str) -> tuple[float, float, str | None]:
        """Returns (compile seconds, link seconds, error log or None)."""
        ctypes = self._ctypes
        status = ctypes.c_int(0)
        log = ctypes.create_string_buffer(16384)
        start = time.perf_counter()
        shader = self.create_shader(GL_STAGE_ENUM[stage])
        text = (ctypes.c_char_p * 1)(source.encode("utf-8"))
        self.shader_source(shader, 1, text, None)
        self.compile_shader(shader)
        self.get_shader_iv(shader, GL_COMPILE_STATUS, ctypes.byref(status))
        compiled = time.perf_counter()
        if not status.value:
            self.get_shader_log(shader, len(log), None, log)
            self.delete_shader(shader)
            return compiled - start, 0.0, "compile: " + log.value.decode(errors="replace")
        program = self.create_program()
        self.attach_shader(program, shader)
        self.link_program(program)
        self.get_program_iv(program, GL_LINK_STATUS, ctypes.byref(status))
        linked = time.perf_counter()
        error = None
        if not status.value:
            self.get_program_log(program, len(log), None, log)
            error = "link: " + log.value.decode(errors="replace")
        self.delete_program(program)
        self.delete_shader(shader)
        return compiled - start, linked - compiled, error


def _resolve_targets(files: list[str], shader_dir: Path) -> list[Path]:
    if not files:
        return select_programs(shader_dir)
    targets = []
    for name in files:
        path = Path(name)
        if not path.is_file():
            path = shader_dir / name
        if not path.is_file():
            raise ValueError(f"no such shader: {name}")
        if stage_for(path.name) is None:
            raise ValueError(f"not a c_/v_/f_ program: {path.name}")
        targets.append(path)
    return targets


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Cold-compile OpenGL shader programs and fail any over the time budget.")
    parser.add_argument("files", nargs="*",
                        help="programs to check (default: every top-level program)")
    parser.add_argument("--shader-dir", type=Path, default=DEFAULT_SHADER_DIR,
                        help="shader tree to read (default: engine/render/src/shaders)")
    parser.add_argument("--budget", type=float, default=DEFAULT_BUDGET_S,
                        help="max compile + link seconds per program (default: %(default)s)")
    args = parser.parse_args(argv)

    if sys.platform != "win32":
        print("shader-compile-verify: native Windows only (WGL); "
              "run it on the Windows ship host", file=sys.stderr)
        return 2
    try:
        targets = _resolve_targets(args.files, args.shader_dir)
    except ValueError as error:
        print(f"shader-compile-verify: {error}", file=sys.stderr)
        return 2
    if not targets:
        print(f"shader-compile-verify: no programs in {args.shader_dir}", file=sys.stderr)
        return 2
    try:
        gl = _GLContext()
    except (OSError, RuntimeError) as error:
        print(f"shader-compile-verify: no GL context: {error}", file=sys.stderr)
        return 2

    print(f"{gl.renderer} | {gl.version}")
    print(f"{'compile':>8} {'link':>8} {'total':>8}  program")
    failures = []
    started = time.perf_counter()
    for path in targets:
        source = inject_nonce(resolve_includes(path), uuid.uuid4().hex)
        compile_s, link_s, error = gl.compile_and_link(source, stage_for(path.name))
        total = compile_s + link_s
        verdict = "FAIL" if error else ("OVER" if total > args.budget else "")
        print(f"{compile_s:8.2f} {link_s:8.2f} {total:8.2f}  {path.name} {verdict}".rstrip(),
              flush=True)
        if error:
            print(error.strip()[:2000], flush=True)
        if verdict:
            failures.append(path.name)
    print(f"{len(targets)} program(s), {time.perf_counter() - started:.1f} s; "
          f"{len(failures)} over the {args.budget:g} s budget or failed"
          + (": " + ", ".join(failures) if failures else ""))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

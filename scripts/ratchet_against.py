"""Split a per-file ratchet's offenders into the ones a change introduced and
the ones it inherited from the tree it was built on.

A ratchet run on a pull request measures the merged tree, so once the base is
red every PR's check is red, and a flat offender list no longer separates the
PR's own regression from the base's. `split` makes that call on each file's
excess over its budget, not on which files are offenders: a PR that worsens an
offender the base already carries has introduced something although the
offender set did not change, and a PR that trims an inherited offender without
clearing it has introduced nothing, so a fix-forward is never blamed.

The base side is read from a git ref's tree, never from a second checkout:
both ratchets resolve their root from `__file__`, so a second tree on disk
would need a second copy of the script.
"""
import io
import subprocess


class RefError(Exception):
    """The `--against` ref, or a blob under it, could not be read."""


def split(head, base):
    """`(introduced, inherited)`, each a sorted list of the paths over budget
    in `head`. Both arguments map a path to `(count, budget)`; a path absent
    from `base` has an excess of 0 there. A path is introduced when its excess
    on the head is above the base's excess floored at 0, else inherited."""
    introduced, inherited = [], []
    for rel, (count, budget) in sorted(head.items()):
        over = count - budget
        if over <= 0:
            continue
        before = base.get(rel)
        prior = max(before[0] - before[1], 0) if before else 0
        (introduced if over > prior else inherited).append(rel)
    return introduced, inherited


def _git(repo, *args, stdin=None):
    try:
        return subprocess.run(["git", "-C", str(repo), *args], input=stdin,
                              capture_output=True, check=True).stdout
    except subprocess.CalledProcessError as e:
        raise RefError(e.stderr.decode(errors="replace").strip() or
                       f"git {' '.join(args)} failed") from e


def ref_files(repo, ref):
    """`{path: blob id}` for every regular file in `ref`'s tree. A symlink
    (mode 120000) and a submodule are skipped, as the working-tree scans skip
    or never reach them."""
    files = {}
    for entry in _git(repo, "ls-tree", "-r", "-z", "--full-tree", ref).split(b"\0"):
        if not entry:
            continue
        meta, path = entry.split(b"\t", 1)
        mode, kind, oid = meta.split()
        if kind == b"blob" and mode != b"120000":
            files[path.decode()] = oid.decode()
    return files


def read_blobs(repo, oids):
    """`{blob id: bytes}` for `oids`, read in one `git cat-file --batch`."""
    wanted = sorted(set(oids))
    if not wanted:
        return {}
    out = _git(repo, "cat-file", "--batch", stdin="".join(o + "\n" for o in wanted).encode())
    blobs, i = {}, 0
    for oid in wanted:
        nl = out.index(b"\n", i)
        header = out[i:nl].split()
        if len(header) != 3:
            raise RefError(f"cannot read blob {oid}")
        size = int(header[2])
        blobs[oid] = out[nl + 1:nl + 1 + size]
        i = nl + 1 + size + 1
    return blobs


def ref_text(repo, ref, rel):
    """The text of `rel` at `ref`, or None when `ref` has no such file."""
    try:
        data = _git(repo, "show", f"{ref}:{rel}")
    except RefError:
        _git(repo, "rev-parse", "--verify", "--quiet", f"{ref}^{{commit}}")
        return None
    return decode(data)


def decode(data):
    """Blob bytes as the text `open(path, errors="replace")` would read."""
    return io.TextIOWrapper(io.BytesIO(data), errors="replace").read()

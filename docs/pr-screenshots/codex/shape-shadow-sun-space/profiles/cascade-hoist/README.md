# Cascade parameter hoist — not adopted

This bounded experiment moves the initial cascade origin, texel size and buffer
offset outside the one/two-sample loop. The optional second iteration switches
directly to the far descriptor. It preserves the single exact-query call site.
The [patch](experiment.patch) includes the shader twins and updated mutations;
it is not part of the production source.

The executable routing tests passed 352 cases per backend, including incorrect
initial far offset and incorrect/missing second-cascade transitions. The PCF
bias tests passed 2,592 cases per backend. Independent review found no routing
defect, but noted the extra mutable state was justified only by measured benefit.

Both arms requested three guarded native Metal runs on the same binary:

```bash
python3 scripts/perf/repeat_profile.py --output /tmp/cascade-hoist-native-before --repeats 3 -- --auto-profile 300 --zoom 1 --yaw 0
python3 scripts/perf/repeat_profile.py --output /tmp/cascade-hoist-native-after --repeats 3 -- --auto-profile 300 --zoom 1 --yaw 0
```

At source revision `f2e764f4a`, the [baseline](baseline/summary.md) completed
all three runs with `RESULT=CLEAN`: mean steady frame time 17.933 ms
(run means 16.190–19.400 ms), mean GPU envelope 3.805 ms, mean GPU sun-shadow
stage 0.041 ms. The experiment's first launch was refused after the quiet-window
drain: [`HOST-NOT-QUIET`, exit 75](attempt/run-1.log). No experimental frame
measurement exists; a baseline alone establishes neither benefit nor regression.
The source and staged assets were restored to the simpler indexed loop.

Retained manifests record source and shader hashes; baseline logs and raw reports
are unmodified. The attempted manifest includes the experimental dirty-tree
state. This native control does not resolve the earlier Linux shadow-stage
overhead or qualify dense scenes. Reconsider the patch only with a paired quiet
measurement, including the backend on which that overhead was observed.

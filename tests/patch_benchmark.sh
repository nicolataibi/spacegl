#!/bin/bash
sed -i 's/if (!run_cull(8192))/struct timespec t1, t2; clock_gettime(CLOCK_MONOTONIC, \&t1); if (!run_cull(8192))/' tests/gdd_mesh_test.c
sed -i 's/\/\* ---------------- Stage 2: EXPAND ---------------- \*\//clock_gettime(CLOCK_MONOTONIC, \&t2); printf("BENCHMARK: cull pass %f ms\\n", (t2.tv_sec - t1.tv_sec)*1000.0 + (t2.tv_nsec - t1.tv_nsec)\/1000000.0); \/\* ---------------- Stage 2: EXPAND ---------------- \*\//' tests/gdd_mesh_test.c

sed -i 's/if (!run_expand(8192))/clock_gettime(CLOCK_MONOTONIC, \&t1); if (!run_expand(8192))/' tests/gdd_mesh_test.c
sed -i 's/\/\* Expected per-pass totals \*\//clock_gettime(CLOCK_MONOTONIC, \&t2); printf("BENCHMARK: expand pass %f ms\\n", (t2.tv_sec - t1.tv_sec)*1000.0 + (t2.tv_nsec - t1.tv_nsec)\/1000000.0); \/\* Expected per-pass totals \*\//' tests/gdd_mesh_test.c

sed -i 's/if (!run_final())/clock_gettime(CLOCK_MONOTONIC, \&t1); if (!run_final())/' tests/gdd_mesh_test.c
sed -i 's/CHECK(u_indirect\[0\].vertexCount == u_counts->opaque_verts/clock_gettime(CLOCK_MONOTONIC, \&t2); printf("BENCHMARK: final pass %f ms\\n", (t2.tv_sec - t1.tv_sec)*1000.0 + (t2.tv_nsec - t1.tv_nsec)\/1000000.0); CHECK(u_indirect[0].vertexCount == u_counts->opaque_verts/' tests/gdd_mesh_test.c

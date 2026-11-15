# bc7e

- Upstream: https://github.com/richgel999/bc7enc_rdo
- Vendored `bc7e.ispc`: commit `b9438627eef73a1157e84201b6fa6eb2ffd6d9f0`, unmodified.
- Copyright: Binomial LLC, Apache-2.0; keep `LICENSE` with distributed binaries.
- Build reference: https://github.com/lcebot/bc7e/blob/main/build.py
  (its image wrapper is not used; EnemyRegistry already owns texture/mip rows).
- Tested compiler: ISPC 1.32.0, build `2b17b0ef24032610`, LLVM 23.1.2.
- Official Windows compiler archive: `ispc-v1.32.0-windows.zip`, SHA256
  `148d5cd96abe7501c730d5fbd00f1f0c893b96bfeada38d1811c9b6251e86a5c`.

Pass `-DBC7E_ISPC_EXECUTABLE=/path/to/ispc.exe` to CMake or put ISPC on PATH.
The compiler is build-only. The archive contains all three instruction-set
objects and their runtime dispatcher; no extra encoder DLL is deployed.
No fast-math or RDO transform is enabled. The upstream exported-function warning
on ISPC 1.32 is retained; the source still uses internal exported-function calls.

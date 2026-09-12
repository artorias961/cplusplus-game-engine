# Environment artwork

Runtime assets are `reference.png` and the six `*-opaque.png` files. The reference is a byte-identical copy of the supplied image. The alternatives were generated with the built-in image tool using that reference, then edited with the same tool to remove painted checkerboards.

The files named `storm.png`, `frozen.png`, `blood.png`, `ashen.png`, `marsh.png`, `desert.png` and `*-layer.png` are retained generation attempts. They contain painted checkerboards and are **not** used by the game. Their names must not be interpreted as proof of transparency.

Sky, mountains and architecture remain together in each final opaque painting. Clouds, moon, low fog, particles, pulses, splashes and weather are independent engine entities. Do not describe these files as fully separated landscape atlases.

See `docs/environment-preview.md` for controls, rendering tradeoffs and verification. The screenshot gallery is `manual_testing/environment_shots/index.html`.

# Xiaoya artwork source

The PNG masters in `xiaoya/` are original assets generated with OpenAI's
built-in Imagegen tool in transparent-background generation mode. They do not
imitate an existing character or product IP.

The anchor prompt specified an original, gender-neutral fantasy sprout creature
for children about to enter primary school: rounded ears, a leaf-sprout tail,
warm teal-and-leaf-green palette, cream face and belly, thick clean outline,
large readable eyes, centered full body, no text, and no props. Follow-up
generations created idle, happy, thinking, surprised, sad, angry, sleepy, and
loving six-pose action families.

`xiaoya/frames/` is the checked-in animation source: eight families, six
independent 256x256 PNGs per family. Every file has a real alpha channel; raw
Imagegen sheets with painted checkerboard previews are not shipped. The one-time
`scripts/prepare_xiaoya_frames.py` helper converts those generated sheets into
small, aligned transparent source frames.

`scripts/build_xiaoya_assets.py` produces the 176x176 runtime GIFs directly from
the transparent frames. Action families play at roughly 11 fps; the idle family
uses longer open-eye pauses around its blink. Do not hand-edit all 21 protocol
emotion GIFs.

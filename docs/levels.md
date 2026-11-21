# Levels

Not yet reverse engineered. Verified so far:

- Land Select lists five lands in this order: The Amazon, Transylvania, African Mines,
  The Himalayas, The Moon. Each Down press moves the cursor one entry
  (`tests/routes/*_intro.txt` reach each land with 0-4 presses).
- At the start of The Amazon, holding Left moves Scrooge's screen X (`$0650`) from `$80` to
  `$44` without the camera moving.

Room data, tilemaps, transitions, camera limits and object placement are open work.

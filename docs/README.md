# docs

Images that other things point at. Both are **generated**, and the commands are
here so neither becomes a file nobody can reproduce.

## `hero.png`

The website's hero for this project, registered in the website repo's
`scripts/shots.json` as `idler/docs/hero.png`.

```bash
./build/idtest --out docs/hero.png --saver 7 --preset 8 --time 55 \
               --size 1600x900 --set "Camera Distance=0.0" --set "Fog=0.25"
```

3D Pipes rather than one of the other ten, because it is the one that is
recognisable at thumbnail size — the flat savers are thin coloured lines on black
and the maze is a wall. `Camera Distance=0.0` overrides the preset's 0.5, which
frames the box for a video where it has room and leaves it small in a still.

Registering it in `shots.json` is not optional bookkeeping: the website's
`make_screens.py` **prunes** anything in `public/screens/` that `shots.json` did
not produce, so a hero committed straight into the website repo is a file waiting
to be deleted on the next run.

## `video-thumb.png`

The README's video poster. Produced by the video toolkit
(`stoatworks-backend/video/projects/idler/build.py`) as `out/idler-thumb.png`,
and the same image is pushed to `stoatworks-labs/thumbnails` as `video/idler.png`
— which is the copy YouTube actually fetches, anonymously, over the public
internet.

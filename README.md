# Pocketspace

**Build interactive tools that live alongside your VR apps.**

Create a VR workspace, a floating control panel, or a scene full of objects you
can pick up and play with—all in JavaScript. Pocketspace runs
alongside your existing SteamVR application, so your tools can stay with you in VR.

[Documentation](https://zetaphor.github.io/pocketspace/) ·
[Getting started](docs/getting-started.md) · [Examples](docs/examples.md)

## What you can build

- **Interactive 3D tools.** Load GLB models, place them around you, and grab,
  rotate or throw objects with either controller using grip, trigger or both.
- **Panels and interfaces.** Draw text, images and controls on canvas panels,
  then respond to controller pointer clicks.
- **Physical scenes.** Add gravity, collisions, joints, springs, motors and
  breakable connections to build moving mechanisms.
- **Animated models.** Play and blend skeletal, morph and node animations with
  independent playback for each object.
- **Lit materials.** Use opt-in PBR lighting and HDR environment reflections,
  with automatic model LOD for distant objects and full detail while held.
- **Persistent workspaces.** Group related objects and panels into containers,
  then save and restore their layout, animation, physics and panel contents.
- **Tracking-aware experiences.** Read controller input, eye gaze and finger
  sensor values, with personal eye-calibration support.

Write scripts against the included API—no game-engine project is required.
Edits reload automatically, and a failed reload keeps your current scene running.
The engine targets Steam Frame on Linux; the [setup guide](docs/getting-started.md)
covers dependencies and build configuration.

## Run your first scene

With the dependencies installed and SteamVR running:

```sh
cmake -S . -B build
cmake --build build -j2
./run-pocketspace.sh scripts/grabbing.js
```

You’ll get a box you can grab using either controller. Start with
an [example script](docs/examples.md), change it, and see the result in your headset.
See [getting started](docs/getting-started.md) for setup and a minimal scene of your own.

## Explore the engine

| Start here | What you’ll find |
| --- | --- |
| [Example scripts](docs/examples.md) | Ready-to-run scenes for interaction, UI, animation, physics, lighting and tracking |
| [JavaScript API](docs/javascript-api.md) | Load models, create objects, handle input and query engine statistics |
| [Feature guides](docs/getting-started.md#where-to-go-next) | Panels, physics, animation, lighting, tracking and scene persistence |
| [Architecture](docs/architecture.md) | The runtime and rendering pipeline, resource ownership and optimization policies |
| [Roadmap](docs/remaining-work.md) | Features and investigations still ahead |
| [Tests and results](tests/index.md) | Automated checks, headset scenes and performance measurements |

## License

[MIT](LICENSE). Third-party code and sample assets retain their respective
licenses; see [third-party credits](docs/getting-started.md#third-party-licenses)
and [sample asset attribution](assets/samples/ATTRIBUTION.md).

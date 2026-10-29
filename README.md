# Pocketspace

**Build games, tools, toys and experiences. AR in VR, powered by JavaScript.**

Pocketspace is a platform for adding your own interactive layer to virtual
reality. Build a floating game, a useful tool, a physics toy or an animated
companion scene that lives alongside the SteamVR apps you already use.

Think of it as augmented reality inside VR: your creations share your virtual
space while another application runs. Start with a few lines of JavaScript,
then bring your ideas to life with objects you can grab, interfaces you can
click and worlds that respond to you.

[Documentation](https://zetaphor.github.io/pocketspace/) ·
[Getting started](docs/getting-started.md) · [Examples](docs/examples.md)

## What you can build

- **Games in your space.** Combine grabbable models, controller input, physics
  and animation to create interactive play alongside your VR apps.
- **Tools within reach.** Build floating panels with text, images and controls,
  or make 3D tools you can pick up and move with either hand.
- **Toys to experiment with.** Give objects gravity and collisions; connect
  them with springs, motors and breakable joints to build playful mechanisms.
- **Experiences that respond to you.** Use controller input, eye gaze and finger
  sensor values to drive interactions, with personal eye-calibration support.
- **Scenes with character.** Play and blend model animations, add PBR materials
  and HDR environment reflections, and arrange objects all around you.
- **Creations you can return to.** Save and restore groups of objects and panels,
  including their layout, animation, physics and panel contents.

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

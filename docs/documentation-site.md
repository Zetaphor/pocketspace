# Documentation site

The documentation is published at
[zetaphor.github.io/sf-overlay-engine](https://zetaphor.github.io/sf-overlay-engine/).
[VitePress](https://vitepress.dev/) builds a static site directly from the Markdown
files in `docs/`. Those files remain readable and editable in the repository.
The site adds a navigation sidebar, local full-text search, syntax highlighting,
light/dark themes and page outlines. No running backend is needed to serve it.

## Preview locally

The documentation tooling requires Node.js 22 or newer and npm. It is separate
from the native engine build and does not need a headset or SteamVR.

```sh
npm ci
npm run docs:dev
```

For a production preview:

```sh
python3 tools/lint-docs.py
npm run docs:build
npm run docs:preview
```

Open the URL printed by the command, including `/sf-overlay-engine/`. Generated
HTML and search assets live in `docs/.vitepress/dist/`; build output, caches and
`node_modules/` stay out of Git. Commit `package-lock.json` with dependency changes.

## Edit the content

Edit existing `docs/*.md` guides normally. The build/run and example pages include
named regions from `README.md`, so commands and the example inventory have one
source. Keep the `quickstart` and `examples` region markers when editing it.
The repository README remains normal GitHub Markdown.

`docs/.vitepress/config.mjs` defines navigation, search, metadata and the GitHub
Pages base path. `docs/.vitepress/theme/` contains the small theme customization.
Links between guides stay on the site. Links to scripts, test evidence and other
repository files become GitHub links during rendering; Markdown sources retain
their ordinary relative links. Test results are linked rather than copied into
the user guides or search index.

When adding a guide, link it from the navigation and README where appropriate.
Run the documentation linter and production build; broken internal page links
fail the build. Check heading anchors when linking to sections.

## GitHub Pages publishing

The public engine repository hosts the Markdown sources and the site. Repository
Settings → Pages uses **GitHub Actions** as its publishing source. The
[documentation workflow](../.github/workflows/docs.yml) installs locked dependencies,
lints and builds on documentation pull requests. Pushes to `main` that affect
documentation automatically build and deploy; workflow dispatch can also publish.
Only the generated static site is uploaded, rather than the engine binaries or
model assets. No custom domain is required.

The workflow grants Pages deployment permissions only to its deployment job and
never deploys pull requests. The project base path is `/sf-overlay-engine/`; if
the repository name or hosting location changes, update that setting and the
published links here and in the README. Native engine code is unaffected by the
documentation build.

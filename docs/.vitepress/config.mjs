import { defineConfig } from 'vitepress'
import { posix } from 'node:path'
import { fileURLToPath } from 'node:url'

const repository = 'https://github.com/Zetaphor/pocketspace'
export default defineConfig({
  title: 'Pocketspace',
  description: 'Build interactive VR tools in JavaScript: workspaces, control panels and hands-on scenes alongside your SteamVR apps.',
  base: '/pocketspace/',
  lang: 'en-US',
  head: [
    ['link', { rel: 'icon', type: 'image/png', sizes: '512x512', href: '/pocketspace/shortcut_icon_512x512.png' }],
    ['link', { rel: 'apple-touch-icon', href: '/pocketspace/shortcut_icon_512x512.png' }],
    // Link previews when the documentation is shared.
    ['meta', { property: 'og:image', content: 'https://zetaphor.github.io/pocketspace/library_header_920x430.png' }],
    ['meta', { name: 'twitter:card', content: 'summary_large_image' }]
  ],
  // The branding images in assets/branding are the site's static files.
  vite: { publicDir: fileURLToPath(new URL('../../assets/branding', import.meta.url)) },
  lastUpdated: true,
  themeConfig: {
    siteTitle: 'Pocketspace',
    logo: '/shortcut_icon_512x512.png',
    nav: [
      { text: 'Get started', link: '/getting-started' },
      { text: 'Examples', link: '/examples' },
      { text: 'Demos', link: '/demos' },
      { text: 'API', link: '/javascript-api' }
    ],
    sidebar: [
      { text: 'Start here', items: [
        { text: 'Overview', link: '/' },
        { text: 'Getting started', link: '/getting-started' },
        { text: 'Example scripts', link: '/examples' },
        { text: 'Demos', link: '/demos' }
      ] },
      { text: 'Build your tool', items: [
        { text: 'JavaScript API', link: '/javascript-api' },
        { text: 'Scene containers', link: '/scene-containers' },
        { text: 'Save and restore', link: '/scene-persistence' },
        { text: 'Panels and UI', link: '/panels' },
        { text: 'Physics', link: '/physics-api' },
        { text: 'Animation', link: '/animation-api' },
        { text: 'Model lighting', link: '/lighting-api' },
        { text: 'Performance options', link: '/optimizations' }
      ] },
      { text: 'Input and runtime', items: [
        { text: 'Tracking', link: '/tracking-api' },
        { text: 'Eye calibration', link: '/eye-calibration' },
        { text: 'Input architecture', link: '/input-architecture' },
        { text: 'Lifecycle', link: '/lifecycle' },
        { text: 'Command line and REPL', link: '/cli' },
        { text: 'SteamVR dashboard', link: '/dashboard' },
        { text: 'Control protocol', link: '/control-protocol' }
      ] },
      { text: 'Project', items: [
        { text: 'Engine architecture', link: '/architecture' },
        { text: 'Roadmap', link: '/remaining-work' },
        { text: 'Maintainer guide', link: '/agent-handoff' },
        { text: 'Documentation site', link: '/documentation-site' },
        { text: 'Tests and results', link: `${repository}/blob/main/tests/index.md` }
      ] }
    ],
    search: { provider: 'local' },
    outline: { level: [2, 3] },
    editLink: { pattern: `${repository}/edit/main/docs/:path`, text: 'Edit this page on GitHub' },
    socialLinks: [{ icon: 'github', link: repository }],
    footer: { message: 'Released under the GNU AGPL v3.0 only.' }
  },
  markdown: {
    config(md) {
      // Keep repository Markdown readable on GitHub. Rewrite only site output,
      // with repository links pointing to their corresponding GitHub files.
      md.core.ruler.after('inline', 'repository-links', state => {
        for (const block of state.tokens) for (const token of block.children || []) {
          if (token.type !== 'link_open') continue
          const href = token.attrGet('href') || ''
          if (href.startsWith('docs/')) token.attrSet('href', './' + href.slice(5))
          else if (href.startsWith('../')) {
            token.attrSet('href', `${repository}/blob/main/${posix.normalize(posix.join('docs', href))}`)
          } else if (/^(scripts|assets|tests)\//.test(href)) {
            token.attrSet('href', `${repository}/blob/main/${href}`)
          }
        }
      })
    }
  }
})

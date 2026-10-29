import { defineConfig } from 'vitepress'
import { posix } from 'node:path'

const repository = 'https://github.com/Zetaphor/sf-overlay-engine'
export default defineConfig({
  title: 'Steam Frame Overlay Engine',
  description: 'Build scripted VR tools with models, animation, physics, interactive panels and tracking.',
  base: '/sf-overlay-engine/',
  lang: 'en-US',
  lastUpdated: true,
  themeConfig: {
    siteTitle: 'Frame Overlay Engine',
    nav: [
      { text: 'Guide', link: '/getting-started' },
      { text: 'API', link: '/javascript-api' },
      { text: 'Examples', link: '/examples' }
    ],
    sidebar: [
      { text: 'Start here', items: [
        { text: 'Build and run', link: '/getting-started' },
        { text: 'Example scripts', link: '/examples' }
      ] },
      { text: 'Build your tool', items: [
        { text: 'JavaScript API', link: '/javascript-api' },
        { text: 'Scene containers', link: '/scene-containers' },
        { text: 'Save and restore', link: '/scene-persistence' },
        { text: 'Panels and UI', link: '/panels' },
        { text: 'Physics', link: '/physics-api' },
        { text: 'Animation', link: '/animation-api' }
      ] },
      { text: 'Input and runtime', items: [
        { text: 'Tracking', link: '/tracking-api' },
        { text: 'Eye calibration', link: '/eye-calibration' },
        { text: 'Input architecture', link: '/input-architecture' },
        { text: 'Lifecycle', link: '/lifecycle' }
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
    footer: { message: 'Released under the MIT License.' }
  },
  markdown: {
    config(md) {
      // Keep repository Markdown readable on GitHub. Rewrite only site output,
      // including README sections embedded in the build/run and examples pages.
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

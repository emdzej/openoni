import { defineConfig } from "vitepress";

export default defineConfig({
  title: "OpenOni",
  description:
    "A faithful reimplementation of Oni (Bungie West, 2001) in C11 for the gasm runtime, using the data from your own copy of the game.",
  cleanUrls: true,
  lastUpdated: true,
  // The reverse-engineering notes (docs/re/) stay in the repository and are not published.
  srcExclude: ["README.md", "AGENTS.md", "re/**"],
  sitemap: { hostname: "https://openoni.emdzej.pl" },

  head: [
    ["meta", { property: "og:title", content: "OpenOni: Oni, reimplemented" }],
    [
      "meta",
      {
        property: "og:description",
        content:
          "A faithful reimplementation of Oni (2001) in C11, ported from the original executable, for gasm: natively and in the browser, with the data from your own copy.",
      },
    ],
    ["meta", { property: "og:url", content: "https://openoni.emdzej.pl/" }],
  ],

  themeConfig: {
    siteTitle: "OpenOni",
    nav: [
      { text: "Guide", link: "/guide/" },
      { text: "Formats", link: "/formats/vise" },
    ],
    sidebar: [
      {
        text: "Guide",
        items: [
          { text: "What OpenOni is", link: "/guide/" },
          { text: "Getting the game files", link: "/guide/game-data" },
          { text: "Status", link: "/guide/status" },
        ],
      },
      {
        text: "Formats",
        items: [{ text: "Installer VISE archive", link: "/formats/vise" }],
      },
    ],
    socialLinks: [{ icon: "github", link: "https://github.com/emdzej/openoni" }],
    footer: {
      message: "GPL-3.0. Oni is a trademark of its owners; this project is not affiliated with them and distributes none of their data.",
    },
  },
});

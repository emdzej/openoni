import { defineConfig } from "vitepress";

export default defineConfig({
  title: "OpenOni",
  description:
    "A faithful reimplementation of Oni (Bungie West, 2001) in C11 for the gasm runtime, using the data from your own copy of the game.",
  cleanUrls: true,
  // Dark first, as the game's menu; the light theme stays a toggle away.
  appearance: "dark",
  lastUpdated: true,
  // The reverse-engineering notes (docs/re/) stay in the repository and are not published.
  srcExclude: ["README.md", "AGENTS.md", "re/**"],
  sitemap: { hostname: "https://openoni.emdzej.pl" },
  // The browser player is a static page in public/play/, not a Markdown page.
  ignoreDeadLinks: [/^\/play\//],

  head: [
    ["link", { rel: "preconnect", href: "https://fonts.googleapis.com" }],
    ["link", { rel: "preconnect", href: "https://fonts.gstatic.com", crossorigin: "" }],
    [
      "link",
      {
        rel: "stylesheet",
        href: "https://fonts.googleapis.com/css2?family=Chakra+Petch:wght@500;600;700&family=Inter:wght@400;500;600&display=swap",
      },
    ],
    ["meta", { name: "theme-color", content: "#000010" }],
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
      // A static app in docs/public/play/: target _self makes it a full page load, not a VitePress route.
      { text: "Play", link: "/play/", target: "_self" },
      { text: "ENCORE", link: "https://encore.emdzej.pl" },
    ],
    sidebar: [
      {
        text: "Guide",
        items: [
          { text: "What OpenOni is", link: "/guide/" },
          { text: "Installing", link: "/guide/install" },
          { text: "Getting the game files", link: "/guide/game-data" },
          { text: "Status", link: "/guide/status" },
          { text: "How Oni works", link: "/guide/theory" },
        ],
      },
      {
        text: "Formats",
        items: [
          { text: "Installer VISE archive", link: "/formats/vise" },
          { text: "Instance files (.dat, .raw)", link: "/formats/dat" },
          { text: "Textures (TXMP)", link: "/formats/txmp" },
          { text: "Geometry (M3GM)", link: "/formats/m3gm" },
          { text: "Environment (AKEV)", link: "/formats/akev" },
          { text: "Animations (TRAM)", link: "/formats/tram" },
          { text: "Characters (ONCC)", link: "/formats/oncc" },
          { text: "Level objects (OBJC)", link: "/formats/objc" },
          { text: "Object animations (OBAN)", link: "/formats/oban" },
          { text: "Weapon classes (ONWC)", link: "/formats/onwc" },
          { text: "Corpses (CRSA)", link: "/formats/crsa" },
          { text: "Sounds (SNDD)", link: "/formats/sndd" },
          { text: "Sound groups (OSBD)", link: "/formats/osbd" },
          { text: "Animation sounds (SABD)", link: "/formats/sabd" },
          { text: "Fonts (TSFF)", link: "/formats/font" },
          { text: "Text (SUBT, IGSt, ...)", link: "/formats/text" },
        ],
      },
    ],
    socialLinks: [{ icon: "github", link: "https://github.com/emdzej/openoni" }],
    footer: {
      message: "Part of <a href=\"https://encore.emdzej.pl\">ENCORE</a> — Engines of Nostalgia, Clean Open REimplementations. GPL-3.0. Oni is a trademark of its owners; this project is not affiliated with them and distributes none of their data. <a href=\"/credits\">Credits</a>",
    },
  },
});

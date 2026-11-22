// node export-graphics.cjs <render directory> [all|plates|captions]
// The render directory receives plates/*.png and captions/cap-NN.png.
const fs = require("node:fs");
const path = require("node:path");
const { pathToFileURL } = require("node:url");

const cardIds = [
  "title", "mary", "fixed", "encounter", "breed", "course", "market",
  "member", "handicap", "wfa", "club", "camp", "dose", "line", "final"
];

function loadPlaywright() {
  const paths = [__dirname, path.resolve(path.dirname(process.execPath), "..")];
  return require(require.resolve("playwright", { paths }));
}

function chromeLocation() {
  const candidates = [
    process.env.CHROME_PATH,
    path.join(process.env.PROGRAMFILES || "", "Google/Chrome/Application/chrome.exe"),
    path.join(process.env["PROGRAMFILES(X86)"] || "", "Microsoft/Edge/Application/msedge.exe")
  ];
  return candidates.find(candidate => candidate && fs.existsSync(candidate));
}

async function writeCardPlates(browser, output) {
  const folder = path.join(output, "plates");
  await fs.promises.mkdir(folder, { recursive: true });
  const page = await browser.newPage({ viewport: { width: 2580, height: 1080 }, deviceScaleFactor: 1 });
  try {
    for (const id of cardIds) {
      const url = pathToFileURL(path.join(__dirname, "cards.html"));
      url.searchParams.set("card", id);
      await page.goto(url.href);
      await page.evaluate(() => document.fonts.ready);
      await page.screenshot({ path: path.join(folder, id + ".png"), omitBackground: true });
    }
  } finally {
    await page.close();
  }
}

async function writeCaptionPlates(browser, output) {
  const entries = JSON.parse(await fs.promises.readFile(path.join(__dirname, "narration.json"), "utf8"));
  const folder = path.join(output, "captions");
  await fs.promises.mkdir(folder, { recursive: true });
  const page = await browser.newPage({ viewport: { width: 2300, height: 180 }, deviceScaleFactor: 1 });
  try {
    await page.goto(pathToFileURL(path.join(__dirname, "captions.html")).href);
    for (const [index, entry] of entries.entries()) {
      if (!entry.zh?.trim() || !entry.en?.trim()) throw new Error("Missing subtitle text at row " + index);
      const fits = await page.evaluate(async ({ zh, en }) => {
        setCaption(zh, en);
        await document.fonts.ready;
        return [...document.querySelectorAll(".caption span")].every(span => {
          const range = document.createRange();
          range.selectNodeContents(span);
          return [...range.getClientRects()].every(r =>
            r.width > 0 && r.left >= 0 && r.top >= 0 && r.right <= 2300 && r.bottom <= 180
          );
        });
      }, entry);
      if (!fits) throw new Error("Subtitle overflows the render region at row " + index);
      const name = "cap-" + String(index).padStart(2, "0") + ".png";
      await page.screenshot({ path: path.join(folder, name), omitBackground: true });
    }
  } finally {
    await page.close();
  }
  console.log("Wrote " + entries.length + " bilingual subtitle plates (2300 × 180, frame offset 140,764).");
}

async function main() {
  const [output, mode = "all"] = process.argv.slice(2);
  if (!output || !["all", "plates", "captions"].includes(mode)) {
    throw new Error("Usage: node export-graphics.cjs <render directory> [all|plates|captions]");
  }
  const { chromium } = loadPlaywright();
  const browser = await chromium.launch({
    executablePath: chromeLocation(), headless: true,
    args: ["--allow-file-access-from-files", "--hide-scrollbars"]
  });
  try {
    if (mode !== "captions") await writeCardPlates(browser, output);
    if (mode !== "plates") await writeCaptionPlates(browser, output);
  } finally {
    await browser.close();
  }
}

main().catch(err => { console.error(err); process.exitCode = 1; });

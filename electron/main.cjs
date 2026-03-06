const { app, BrowserWindow, shell } = require("electron");
const path = require("path");

function createWindow() {
  const win = new BrowserWindow({
    width: 1200,
    height: 800,
    icon: path.join(__dirname, "..", "build","han_256.ico"),
    webPreferences: {
      // Keep security sane:
      nodeIntegration: false,
      contextIsolation: true,
      sandbox: true,
      // preload can be added later if you need native features
      // preload: path.join(__dirname, "preload.cjs"),
    },
  });

  // Load the built web app
  win.loadFile(path.join(__dirname, "..", "dist", "index.html"));

  // Open external links in the default browser
  win.webContents.setWindowOpenHandler(({ url }) => {
    shell.openExternal(url);
    return { action: "deny" };
  });
}

app.whenReady().then(() => {
  createWindow();

  app.on("activate", () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow();
  });
});

app.on("window-all-closed", () => {
  if (process.platform !== "darwin") app.quit();
});
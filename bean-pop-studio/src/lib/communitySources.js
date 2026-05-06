import { createSampleDataUrl } from "./beadEngine.js";

function createCanvasDataUrl(width, height, draw) {
  const canvas = document.createElement("canvas");
  canvas.width = width;
  canvas.height = height;
  const context = canvas.getContext("2d");
  draw(context, width, height);
  return canvas.toDataURL("image/png");
}

function fillEllipse(context, x, y, radiusX, radiusY, color, rotation = 0) {
  context.fillStyle = color;
  context.beginPath();
  context.ellipse(x, y, radiusX, radiusY, rotation, 0, Math.PI * 2);
  context.fill();
}

function strokePath(context, color, width, draw) {
  context.strokeStyle = color;
  context.lineWidth = width;
  context.lineCap = "round";
  context.lineJoin = "round";
  context.beginPath();
  draw();
  context.stroke();
}

function createPetPatchDataUrl() {
  return createCanvasDataUrl(760, 760, (ctx, width, height) => {
    const wash = ctx.createLinearGradient(0, 0, width, height);
    wash.addColorStop(0, "#fff7e8");
    wash.addColorStop(0.52, "#f3f7ef");
    wash.addColorStop(1, "#dcecf8");
    ctx.fillStyle = wash;
    ctx.fillRect(0, 0, width, height);

    fillEllipse(ctx, width * 0.23, height * 0.2, 88, 66, "#ffd369", -0.2);
    fillEllipse(ctx, width * 0.79, height * 0.18, 102, 76, "#cbe7db", 0.18);
    fillEllipse(ctx, width * 0.5, height * 0.52, 222, 214, "#a77858");
    fillEllipse(ctx, width * 0.5, height * 0.59, 186, 154, "#f2d8bf");
    fillEllipse(ctx, width * 0.34, height * 0.3, 70, 126, "#8e6142", -0.42);
    fillEllipse(ctx, width * 0.66, height * 0.3, 70, 126, "#8e6142", 0.42);
    fillEllipse(ctx, width * 0.34, height * 0.31, 42, 86, "#f2d8bf", -0.4);
    fillEllipse(ctx, width * 0.66, height * 0.31, 42, 86, "#f2d8bf", 0.4);
    fillEllipse(ctx, width * 0.4, height * 0.5, 30, 42, "#1c2330");
    fillEllipse(ctx, width * 0.6, height * 0.5, 30, 42, "#1c2330");
    fillEllipse(ctx, width * 0.39, height * 0.49, 10, 13, "#ffffff");
    fillEllipse(ctx, width * 0.59, height * 0.49, 10, 13, "#ffffff");
    fillEllipse(ctx, width * 0.5, height * 0.58, 42, 34, "#2d2321");
    fillEllipse(ctx, width * 0.5, height * 0.61, 72, 44, "#f7e9d7");
    fillEllipse(ctx, width * 0.47, height * 0.58, 10, 12, "#1a1a1a");
    fillEllipse(ctx, width * 0.53, height * 0.58, 10, 12, "#1a1a1a");

    strokePath(ctx, "#1f1d1b", 10, () => {
      ctx.moveTo(width * 0.45, height * 0.65);
      ctx.quadraticCurveTo(width * 0.5, height * 0.69, width * 0.55, height * 0.65);
    });

    ctx.fillStyle = "#456ed8";
    ctx.beginPath();
    ctx.roundRect(width * 0.24, height * 0.72, width * 0.52, height * 0.16, 44);
    ctx.fill();

    ctx.fillStyle = "#fffaf2";
    ctx.font = `900 ${Math.max(24, width * 0.04)}px "Space Grotesk", "Noto Sans SC", sans-serif`;
    ctx.fillText("PET DAY", width * 0.34, height * 0.815);
  });
}

function createFamilySunDataUrl() {
  return createCanvasDataUrl(980, 720, (ctx, width, height) => {
    const wash = ctx.createLinearGradient(0, 0, width, height);
    wash.addColorStop(0, "#fff8ea");
    wash.addColorStop(0.46, "#e6f5ee");
    wash.addColorStop(1, "#c9dff8");
    ctx.fillStyle = wash;
    ctx.fillRect(0, 0, width, height);

    fillEllipse(ctx, width * 0.78, height * 0.18, 86, 86, "#f4c448");
    ctx.fillStyle = "#8cc37b";
    ctx.fillRect(0, height * 0.72, width, height * 0.28);

    fillEllipse(ctx, width * 0.36, height * 0.52, 92, 140, "#f1c19c");
    fillEllipse(ctx, width * 0.56, height * 0.5, 100, 152, "#efb48e");
    fillEllipse(ctx, width * 0.35, height * 0.37, 64, 82, "#3e2f2a");
    fillEllipse(ctx, width * 0.57, height * 0.35, 70, 90, "#2d2f42");
    fillEllipse(ctx, width * 0.36, height * 0.43, 52, 62, "#f6d8bf");
    fillEllipse(ctx, width * 0.56, height * 0.41, 56, 68, "#f7dcc3");
    fillEllipse(ctx, width * 0.34, height * 0.43, 8, 12, "#1f2230");
    fillEllipse(ctx, width * 0.38, height * 0.43, 8, 12, "#1f2230");
    fillEllipse(ctx, width * 0.54, height * 0.41, 8, 12, "#1f2230");
    fillEllipse(ctx, width * 0.58, height * 0.41, 8, 12, "#1f2230");

    strokePath(ctx, "#7b4e34", 8, () => {
      ctx.moveTo(width * 0.34, height * 0.49);
      ctx.quadraticCurveTo(width * 0.36, height * 0.52, width * 0.39, height * 0.49);
      ctx.moveTo(width * 0.53, height * 0.47);
      ctx.quadraticCurveTo(width * 0.56, height * 0.5, width * 0.59, height * 0.47);
    });

    ctx.fillStyle = "#ff633d";
    ctx.beginPath();
    ctx.roundRect(width * 0.27, height * 0.58, width * 0.18, height * 0.16, 34);
    ctx.fill();
    ctx.fillStyle = "#456ed8";
    ctx.beginPath();
    ctx.roundRect(width * 0.47, height * 0.56, width * 0.2, height * 0.18, 38);
    ctx.fill();

    ctx.fillStyle = "#f9f2dc";
    ctx.beginPath();
    ctx.roundRect(width * 0.18, height * 0.74, width * 0.64, height * 0.1, 28);
    ctx.fill();
    ctx.strokeStyle = "#d9c7ad";
    ctx.lineWidth = 3;
    ctx.stroke();

    ctx.fillStyle = "#00a77e";
    ctx.beginPath();
    ctx.moveTo(width * 0.18, height * 0.79);
    ctx.lineTo(width * 0.82, height * 0.79);
    ctx.lineWidth = 8;
    ctx.strokeStyle = "#00a77e";
    ctx.stroke();
  });
}

function createGiftBearDataUrl() {
  return createCanvasDataUrl(720, 920, (ctx, width, height) => {
    const wash = ctx.createLinearGradient(0, 0, width, height);
    wash.addColorStop(0, "#fff8ee");
    wash.addColorStop(0.5, "#fde7dc");
    wash.addColorStop(1, "#f0f6ff");
    ctx.fillStyle = wash;
    ctx.fillRect(0, 0, width, height);

    fillEllipse(ctx, width * 0.24, height * 0.16, 76, 56, "#f0c348", -0.2);
    fillEllipse(ctx, width * 0.75, height * 0.14, 88, 64, "#cce6d8", 0.22);

    fillEllipse(ctx, width * 0.5, height * 0.42, 168, 178, "#9f7154");
    fillEllipse(ctx, width * 0.38, height * 0.26, 54, 54, "#9f7154");
    fillEllipse(ctx, width * 0.62, height * 0.26, 54, 54, "#9f7154");
    fillEllipse(ctx, width * 0.38, height * 0.26, 28, 28, "#f4d8be");
    fillEllipse(ctx, width * 0.62, height * 0.26, 28, 28, "#f4d8be");
    fillEllipse(ctx, width * 0.5, height * 0.46, 124, 112, "#f3dbc8");
    fillEllipse(ctx, width * 0.44, height * 0.39, 16, 22, "#1f2230");
    fillEllipse(ctx, width * 0.56, height * 0.39, 16, 22, "#1f2230");
    fillEllipse(ctx, width * 0.5, height * 0.47, 28, 22, "#2d2321");

    strokePath(ctx, "#2d2321", 9, () => {
      ctx.moveTo(width * 0.44, height * 0.53);
      ctx.quadraticCurveTo(width * 0.5, height * 0.58, width * 0.56, height * 0.53);
    });

    ctx.fillStyle = "#ff633d";
    ctx.beginPath();
    ctx.roundRect(width * 0.18, height * 0.6, width * 0.64, height * 0.2, 44);
    ctx.fill();
    ctx.fillStyle = "#f0c348";
    ctx.beginPath();
    ctx.roundRect(width * 0.47, height * 0.55, width * 0.06, height * 0.32, 24);
    ctx.fill();
    ctx.beginPath();
    ctx.roundRect(width * 0.26, height * 0.68, width * 0.48, height * 0.06, 24);
    ctx.fill();

    strokePath(ctx, "#f0c348", 18, () => {
      ctx.moveTo(width * 0.5, height * 0.55);
      ctx.bezierCurveTo(width * 0.44, height * 0.5, width * 0.38, height * 0.53, width * 0.4, height * 0.6);
      ctx.moveTo(width * 0.5, height * 0.55);
      ctx.bezierCurveTo(width * 0.56, height * 0.5, width * 0.62, height * 0.53, width * 0.6, height * 0.6);
    });

    ctx.fillStyle = "#1f1d1b";
    ctx.font = `900 ${Math.max(30, width * 0.052)}px "Space Grotesk", "Noto Sans SC", sans-serif`;
    ctx.fillText("FOR YOU", width * 0.3, height * 0.86);
  });
}

const sourceFactories = {
  "sample-pop": () => createSampleDataUrl(),
  "pet-patch": createPetPatchDataUrl,
  "family-sun": createFamilySunDataUrl,
  "gift-bear": createGiftBearDataUrl,
};

const sourceSubjectBoxes = {
  "sample-pop": {
    x: 0.14,
    y: 0.12,
    width: 0.72,
    height: 0.74,
    confidence: 0.92,
  },
  "pet-patch": {
    x: 0.14,
    y: 0.12,
    width: 0.72,
    height: 0.76,
    confidence: 0.9,
  },
  "family-sun": {
    x: 0.16,
    y: 0.16,
    width: 0.66,
    height: 0.68,
    confidence: 0.88,
  },
  "gift-bear": {
    x: 0.17,
    y: 0.1,
    width: 0.66,
    height: 0.8,
    confidence: 0.91,
  },
};

export function createCommunitySourceDataUrl(sourceKind) {
  const factory = sourceFactories[sourceKind] || sourceFactories["sample-pop"];
  return factory();
}

export function getCommunitySourceSubjectBox(sourceKind) {
  const subjectBox = sourceSubjectBoxes[sourceKind] || sourceSubjectBoxes["sample-pop"];
  return {
    ...subjectBox,
  };
}

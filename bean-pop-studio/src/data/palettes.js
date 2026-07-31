import paletteConfig from "./palettes.json" with { type: "json" };

const DEFAULTS = {
  available: true,
  autoMatch: true,
  finish: "solid",
  material: "midi-5mm",
  ...(paletteConfig.defaults || {}),
};

const GROUP_META = paletteConfig.groups || {};

function normalizeHex(hex, code) {
  const normalized = String(hex || "").trim().toLowerCase();
  if (!/^#[0-9a-f]{6}$/.test(normalized)) {
    throw new Error(`Invalid palette hex for ${code || "unknown color"}: ${hex}`);
  }
  return normalized;
}

function normalizeString(value, fallback = "") {
  return String(value || fallback).trim();
}

function createColor({
  code,
  name,
  hex,
  group,
  aliases = [],
  available = DEFAULTS.available,
  autoMatch = DEFAULTS.autoMatch,
  finish = DEFAULTS.finish,
  material = DEFAULTS.material,
}) {
  const colorCode = normalizeString(code);
  const groupId = normalizeString(group);
  const groupMeta = GROUP_META[groupId] || {};

  if (!colorCode) {
    throw new Error("Palette color is missing a code");
  }
  if (!groupId) {
    throw new Error(`Palette color ${colorCode} is missing a group`);
  }

  return {
    aliases: Array.isArray(aliases) ? aliases : [],
    autoMatch: autoMatch !== false,
    available: available !== false,
    code: colorCode,
    family: groupId,
    familyLabel: groupMeta.label || groupId,
    finish: normalizeString(finish, DEFAULTS.finish),
    group: groupId,
    hex: normalizeHex(hex, colorCode),
    material: normalizeString(material, DEFAULTS.material),
    name: normalizeString(name, colorCode),
    toneBucket: groupMeta.bucket || "mixed",
  };
}

function normalizeCount(value, fallback) {
  return Math.round(Number(value) || fallback);
}

function createBrand({
  id,
  label,
  shortLabel = id,
  note = "",
  badge = "",
  paletteEdition = "",
  coverage = "starter",
  marketFocus = "",
  maxOutputColors,
  defaultMaxColors = 18,
  recommendedColorCounts = [],
  colors = [],
}) {
  const brandId = normalizeString(id);
  const normalizedColors = colors.map(createColor);
  const paletteSize = normalizedColors.length;

  if (!brandId) {
    throw new Error("Palette brand is missing an id");
  }
  if (!paletteSize) {
    throw new Error(`Palette brand ${brandId} has no colors`);
  }

  const safeMaxOutputColors = Math.min(
    paletteSize,
    Math.max(4, normalizeCount(maxOutputColors, paletteSize))
  );
  const safeDefaultMaxColors = Math.min(
    safeMaxOutputColors,
    Math.max(4, normalizeCount(defaultMaxColors, safeMaxOutputColors))
  );
  const safeRecommendedCounts = [...new Set(
    recommendedColorCounts
      .map((count) => normalizeCount(count, 0))
      .filter((count) => count >= 4 && count <= safeMaxOutputColors)
      .concat(safeDefaultMaxColors, safeMaxOutputColors)
  )].sort((left, right) => left - right);

  return {
    badge,
    colors: normalizedColors,
    coverage,
    defaultMaxColors: safeDefaultMaxColors,
    id: brandId,
    label: normalizeString(label, brandId),
    marketFocus,
    maxOutputColors: safeMaxOutputColors,
    note,
    paletteEdition,
    paletteSize,
    recommendedColorCounts: safeRecommendedCounts,
    shortLabel: normalizeString(shortLabel, brandId),
  };
}

function createCatalog(config) {
  const brands = config.brands || {};
  const order = Array.isArray(config.brandOrder) && config.brandOrder.length
    ? config.brandOrder
    : Object.keys(brands);

  return Object.fromEntries(order.map((brandId) => {
    const brand = brands[brandId];
    if (!brand) {
      throw new Error(`Palette brand ${brandId} is listed in brandOrder but missing from brands`);
    }
    return [brandId, createBrand({ id: brandId, ...brand })];
  }));
}

export const brandCatalog = createCatalog(paletteConfig);

export const brandOrder = Object.keys(brandCatalog);

export const palettes = Object.fromEntries(
  brandOrder.map((brandId) => [brandId, brandCatalog[brandId].colors])
);

export const brandMeta = Object.fromEntries(
  brandOrder.map((brandId) => {
    const {
      colors: _colors,
      ...meta
    } = brandCatalog[brandId];
    return [brandId, meta];
  })
);

export function getBrandDefinition(brandId) {
  return brandCatalog[brandId] || brandCatalog.MARD;
}

export function getBrandPalette(brandId) {
  return getBrandDefinition(brandId).colors;
}

export function getBrandPaletteSize(brandId) {
  return getBrandDefinition(brandId).paletteSize;
}

export function getBrandOutputColorLimit(brandId) {
  return getBrandDefinition(brandId).maxOutputColors;
}

export function getBrandDefaultMaxColors(brandId) {
  return getBrandDefinition(brandId).defaultMaxColors;
}

export function getBrandRecommendedColorCounts(brandId) {
  return getBrandDefinition(brandId).recommendedColorCounts;
}

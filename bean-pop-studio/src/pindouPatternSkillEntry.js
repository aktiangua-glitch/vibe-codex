import {
  PINDOU_PATTERN_SKILL_DEFAULTS,
  downloadPindouPattern,
  generatePindouPattern,
  loadPatternImage,
} from "./lib/patternSkill.js";

const api = {
  defaults: PINDOU_PATTERN_SKILL_DEFAULTS,
  download: downloadPindouPattern,
  generate: generatePindouPattern,
  loadImage: loadPatternImage,
  version: "0.1.0",
};

if (typeof window !== "undefined") {
  window.PindouPatternSkill = api;
}

export {
  PINDOU_PATTERN_SKILL_DEFAULTS,
  api as PindouPatternSkill,
  downloadPindouPattern,
  generatePindouPattern,
  loadPatternImage,
};

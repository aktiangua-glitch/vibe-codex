const DEFAULT_SUBJECT_BOX = {
  confidence: 0,
  height: 0.72,
  width: 0.6,
};

export const DEFAULT_BOARD_GUIDE_SIZES = [29, 58, 87, 116, 145];
const DEFAULT_LARGER_SIZE_BIAS = 14;

function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

function pickPreferredBoardSize(value, boardSizes) {
  const nextLarger = boardSizes.find((size) => size >= value);
  return nextLarger || boardSizes[boardSizes.length - 1];
}

function normalizeSubjectMetrics(subjectBox = {}) {
  const width = clamp(
    Number.isFinite(subjectBox.width) ? subjectBox.width : DEFAULT_SUBJECT_BOX.width,
    0.12,
    1
  );
  const height = clamp(
    Number.isFinite(subjectBox.height) ? subjectBox.height : DEFAULT_SUBJECT_BOX.height,
    0.12,
    1
  );

  return {
    confidence: clamp(
      Number.isFinite(subjectBox.confidence) ? subjectBox.confidence : DEFAULT_SUBJECT_BOX.confidence,
      0,
      1
    ),
    coverage: clamp(width * height, 0.08, 1),
    dominantFill: Math.max(width, height),
    visibleRatio: Number.isFinite(subjectBox.visibleRatio)
      ? clamp(subjectBox.visibleRatio, 0.02, 1)
      : null,
  };
}

export function getRecommendedTargetWidth({
  boardSizes = DEFAULT_BOARD_GUIDE_SIZES,
  imageHeight,
  imageWidth,
  maxTargetSize = 145,
  subjectBox = DEFAULT_SUBJECT_BOX,
}) {
  if (!imageWidth || !imageHeight) return 87;

  const availableSizes = boardSizes.filter((size) => size <= maxTargetSize);
  const guideSizes = availableSizes.length ? availableSizes : [boardSizes[0] || 29];
  const longSide = Math.max(imageWidth, imageHeight);
  const shortSide = Math.max(1, Math.min(imageWidth, imageHeight));
  const aspect = longSide / shortSide;
  let size = 58;

  if (longSide >= 4200) size = 145;
  else if (longSide >= 3000) size = 116;
  else if (longSide >= 1400) size = 87;
  else if (longSide < 960) size = 29;

  size += DEFAULT_LARGER_SIZE_BIAS;

  if (aspect >= 1.45) size += 12;
  if (aspect >= 1.85) size += 10;

  const {
    confidence,
    coverage,
    dominantFill,
    visibleRatio,
  } = normalizeSubjectMetrics(subjectBox);

  let boost = 0;
  if (coverage <= 0.18 || dominantFill <= 0.42) boost = 58;
  else if (coverage <= 0.28 || dominantFill <= 0.56) boost = 40;
  else if (coverage <= 0.42 || dominantFill <= 0.68) boost = 24;
  else if (coverage <= 0.56 || dominantFill <= 0.78) boost = 12;

  if (visibleRatio !== null && visibleRatio <= 0.16) boost += 18;
  else if (visibleRatio !== null && visibleRatio <= 0.28) boost += 10;

  if (confidence < 0.24) boost = Math.max(0, boost - 12);
  else if (confidence < 0.42) boost = Math.max(0, boost - 6);

  return pickPreferredBoardSize(
    clamp(size + boost, guideSizes[0], maxTargetSize),
    guideSizes
  );
}

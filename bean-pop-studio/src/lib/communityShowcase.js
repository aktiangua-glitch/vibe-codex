const ALL_CATEGORY = "全部";

export function filterCommunitySeeds(seeds, category = ALL_CATEGORY) {
  if (!Array.isArray(seeds)) return [];
  if (!category || category === ALL_CATEGORY) {
    return seeds.slice();
  }

  return seeds.filter((seed) => seed.category === category);
}

export function getCommunityFeaturedSeeds(seeds, limit = 6) {
  if (!Array.isArray(seeds)) return [];

  return seeds
    .filter((seed) => Number.isFinite(seed.featuredOrder) && seed.featuredOrder > 0)
    .sort((left, right) => left.featuredOrder - right.featuredOrder)
    .slice(0, limit);
}

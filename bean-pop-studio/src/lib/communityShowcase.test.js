import { describe, expect, it } from "vitest";
import { communitySeeds } from "../data/communitySeeds.js";
import { filterCommunitySeeds, getCommunityFeaturedSeeds } from "./communityShowcase.js";

describe("communityShowcase", () => {
  it("returns every seed when category is 全部", () => {
    expect(filterCommunitySeeds(communitySeeds, "全部")).toHaveLength(communitySeeds.length);
  });

  it("filters seeds by a single category", () => {
    const petSeeds = filterCommunitySeeds(communitySeeds, "宠物");

    expect(petSeeds.length).toBeGreaterThan(0);
    expect(petSeeds.every((seed) => seed.category === "宠物")).toBe(true);
  });

  it("returns featured seeds in featured order", () => {
    const featured = getCommunityFeaturedSeeds(communitySeeds, 6);

    expect(featured).toHaveLength(6);
    expect(featured.map((seed) => seed.featuredOrder)).toEqual([1, 2, 3, 4, 5, 6]);
  });
});

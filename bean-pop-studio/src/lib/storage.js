const STORAGE_KEY = "bean_pop_studio_saved_projects_v1";
const ESTIMATOR_KEY = "bean_pop_studio_estimator_v1";

export function loadSavedProjects() {
  try {
    const raw = window.localStorage.getItem(STORAGE_KEY);
    return raw ? JSON.parse(raw) : [];
  } catch {
    return [];
  }
}

export function persistSavedProjects(projects) {
  window.localStorage.setItem(STORAGE_KEY, JSON.stringify(projects));
}

export function loadEstimatorSettings() {
  try {
    const raw = window.localStorage.getItem(ESTIMATOR_KEY);
    return raw ? JSON.parse(raw) : null;
  } catch {
    return null;
  }
}

export function persistEstimatorSettings(settings) {
  window.localStorage.setItem(ESTIMATOR_KEY, JSON.stringify(settings));
}

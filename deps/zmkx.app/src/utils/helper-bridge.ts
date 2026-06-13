export type HelperSchemaField = {
  key: string;
  label: string;
  type: 'text' | 'url' | 'path' | 'command';
  required?: boolean;
  placeholder?: string;
};

export type HelperActionCatalogItem = {
  code: number;
  moduleId: string;
  actionId: string;
  displayName: string;
  category: string;
  icon: string;
  schema: HelperSchemaField[];
};

export type HelperProfile = {
  id: number;
  actionCode: number;
  payload: Record<string, unknown>;
};

export type HelperProfilesExport = {
  version: 1;
  exportedAt: string;
  nextProfileId: number;
  profiles: HelperProfile[];
};

export type HelperExecutionEvent = {
  seq: number;
  slotIndex: number;
  actionCode: number;
  arg0: number;
  arg1: number;
  arg2: number;
  flags: number;
};

export type HelperExecutionResult = {
  seq: number;
  slotIndex: number;
  actionCode: number;
  ok: boolean;
  error?: string;
};

export type HelperHealthStatus = {
  ok: boolean;
  version?: string;
};

const HELPER_BASE_URL = 'http://127.0.0.1:8755';
const REQUEST_TIMEOUT_MS = 1200;
// Must match tools/hw75-helper/src/server.mjs HELPER_VERSION.
export const EXPECTED_HELPER_VERSION = '0.3.0';

export async function getHelperHealth(): Promise<boolean> {
  const status = await getHelperHealthStatus();
  return status.ok;
}

export async function getHelperHealthStatus(): Promise<HelperHealthStatus> {
  try {
    const response = await request<HelperHealthStatus>('/api/health');
    return {
      ok: !!response.ok,
      version: response.version,
    };
  } catch {
    return { ok: false };
  }
}

export async function getHelperCatalog(): Promise<HelperActionCatalogItem[]> {
  const response = await request<{ actions: HelperActionCatalogItem[] }>('/api/catalog');
  return response.actions ?? [];
}

export async function getHelperProfile(profileId: number): Promise<HelperProfile | undefined> {
  if (!profileId) {
    return undefined;
  }

  try {
    return await request<HelperProfile>(`/api/profiles/${profileId}`);
  } catch {
    return undefined;
  }
}

export async function upsertHelperProfile(input: {
  profileId?: number;
  actionCode: number;
  payload: Record<string, unknown>;
}): Promise<HelperProfile> {
  return request<HelperProfile>('/api/profiles/upsert', {
    method: 'POST',
    body: JSON.stringify(input),
  });
}

export async function exportHelperProfiles(): Promise<HelperProfilesExport> {
  return request<HelperProfilesExport>('/api/profiles/export');
}

export async function importHelperProfiles(data: HelperProfilesExport, replace = false): Promise<{
  importedCount: number;
  nextProfileId: number;
}> {
  return request('/api/profiles/import', {
    method: 'POST',
    body: JSON.stringify({ data, replace }),
  });
}

export async function executeHelperEvents(events: HelperExecutionEvent[]): Promise<HelperExecutionResult[]> {
  if (!events.length) {
    return [];
  }

  const response = await request<{ results: HelperExecutionResult[] }>('/api/events/execute', {
    method: 'POST',
    body: JSON.stringify({ events }),
  });
  return response.results ?? [];
}

export async function restartHelper(): Promise<void> {
  await request('/api/restart', {
    method: 'POST',
    body: JSON.stringify({}),
  });
}

async function request<T = unknown>(pathname: string, init?: RequestInit, baseUrl = HELPER_BASE_URL): Promise<T> {
  const controller = new AbortController();
  const timer = window.setTimeout(() => controller.abort(), REQUEST_TIMEOUT_MS);

  try {
    const response = await fetch(`${baseUrl}${pathname}`, {
      ...init,
      headers: {
        'Content-Type': 'application/json',
        ...(init?.headers ?? {}),
      },
      signal: controller.signal,
    });

    if (!response.ok) {
      throw new Error(`${response.status} ${response.statusText}`);
    }

    return await response.json() as T;
  } finally {
    window.clearTimeout(timer);
  }
}

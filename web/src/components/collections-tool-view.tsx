import { useEffect, useState } from "react";

import { buildDownloadUrl, deleteItem, getBrowserAll, readTextFile, uploadFiles } from "../lib/api";
import { CollectionEditorModal } from "./collection-editor-modal";

type CollectionItem = {
  fileName: string;
  iconPath: string | null;
  name: string;
  romPaths: string[];
};

function isTextCollection(name: string): boolean {
  return name.toLowerCase().endsWith(".txt");
}

function stripExtension(name: string): string {
  const dot = name.lastIndexOf(".");

  return dot > 0 ? name.slice(0, dot) : name;
}

async function pickSingleFile(accept: string): Promise<File | null> {
  return new Promise((resolve) => {
    const input = document.createElement("input");

    input.type = "file";
    input.accept = accept;
    input.addEventListener("change", () => {
      resolve(input.files?.[0] ?? null);
    });
    input.click();
  });
}

export function CollectionsToolView({
  csrf,
  onBack,
}: {
  csrf: string | null;
  onBack: () => void;
}) {
  const [collections, setCollections] = useState<CollectionItem[]>([]);
  const [backgroundPath, setBackgroundPath] = useState<string | null>(null);
  const [loading, setLoading] = useState(true);
  const [notice, setNotice] = useState<string | null>(null);
  const [editor, setEditor] = useState<CollectionItem | null>(null);
  const [busyName, setBusyName] = useState<string | null>(null);

  useEffect(() => {
    let active = true;

    async function loadCollections() {
      setLoading(true);
      setNotice(null);

      try {
        if (!csrf) {
          throw new Error("Missing session csrf token.");
        }

        const root = await getBrowserAll("files", csrf, undefined, "Collections");
        const mediaExists = root.entries.some((entry) => entry.type === "directory" && entry.name === ".media");
        const iconPaths = new Map<string, string>();
        let nextBackgroundPath: string | null = null;

        if (mediaExists) {
          try {
            const media = await getBrowserAll("files", csrf, undefined, "Collections/.media");

            for (const entry of media.entries) {
              if (entry.type === "directory") {
                continue;
              }
              if (entry.name === "bg.png") {
                nextBackgroundPath = buildDownloadUrl("files", entry.path, undefined, csrf);
              } else if (entry.name.toLowerCase().endsWith(".png")) {
                iconPaths.set(stripExtension(entry.name), entry.path);
              }
            }
          } catch {
            // Leave icons/background empty if the media folder cannot be read.
          }
        }

        const nextCollections = await Promise.all(
          root.entries
            .filter((entry) => entry.type !== "directory" && isTextCollection(entry.name))
            .map(async (entry) => {
              const content = await readTextFile("files", entry.path, csrf);
              const romPaths = content
                .split("\n")
                .map((line) => line.trim())
                .filter((line) => line.length > 0);
              const name = stripExtension(entry.name);
              const iconPath = iconPaths.get(name) ?? null;

              return {
                fileName: entry.name,
                iconPath,
                name,
                romPaths,
              };
            }),
        );

        if (active) {
          setCollections(nextCollections);
          setBackgroundPath(nextBackgroundPath);
        }
      } catch (error) {
        if (active) {
          setCollections([]);
          setBackgroundPath(null);
          setNotice(error instanceof Error ? error.message : "Could not load collections.");
        }
      } finally {
        if (active) {
          setLoading(false);
        }
      }
    }

    void loadCollections();
    return () => {
      active = false;
    };
  }, [busyName, csrf]);

  async function createCollection() {
    if (!csrf) {
      return;
    }

    const name = window.prompt("Collection name");

    if (!name) {
      return;
    }

    const trimmed = name.trim();

    if (!trimmed || trimmed.includes("/") || trimmed.includes("\\")) {
      setNotice("Collection names cannot contain slashes.");
      return;
    }

    setBusyName(trimmed);
    setNotice(null);
    try {
      await uploadFiles(
        {
          scope: "files",
          path: "Collections",
          files: [new File([""], `${trimmed}.txt`, { type: "text/plain" })],
        },
        csrf,
      );
    } catch (error) {
      setNotice(error instanceof Error ? error.message : "Collection creation failed.");
      setBusyName(null);
      return;
    }
    setBusyName(null);
  }

  async function uploadNamedPng(path: string, fileName: string) {
    if (!csrf) {
      return;
    }

    const file = await pickSingleFile(".png");

    if (!file) {
      return;
    }

    const renamed = new File([await file.arrayBuffer()], fileName, { type: file.type || "image/png" });

    setBusyName(fileName);
    setNotice(null);
    try {
      await uploadFiles({ scope: "files", path, files: [renamed] }, csrf);
    } catch (error) {
      setNotice(error instanceof Error ? error.message : "Upload failed.");
      setBusyName(null);
      return;
    }
    setBusyName(null);
  }

  async function removePath(path: string, label: string) {
    if (!csrf) {
      return;
    }

    if (!window.confirm(`Delete ${label}?`)) {
      return;
    }

    setBusyName(path);
    setNotice(null);
    try {
      await deleteItem({ scope: "files", path }, csrf);
    } catch (error) {
      setNotice(error instanceof Error ? error.message : "Delete failed.");
      setBusyName(null);
      return;
    }
    setBusyName(null);
  }

  async function deleteCollection(collection: CollectionItem) {
    if (!csrf || !window.confirm(`Delete ${collection.name}?`)) {
      return;
    }

    setBusyName(collection.fileName);
    setNotice(null);
    try {
      await deleteItem({ scope: "files", path: `Collections/${collection.fileName}` }, csrf);
      if (collection.iconPath) {
        try {
          await deleteItem({ scope: "files", path: collection.iconPath }, csrf);
        } catch {
          // Leave orphan cleanup best-effort so collection deletion still succeeds.
        }
      }
    } catch (error) {
      setNotice(error instanceof Error ? error.message : "Delete failed.");
      setBusyName(null);
      return;
    }
    setBusyName(null);
  }

  return (
    <div className="space-y-6">
      <div className="flex items-start justify-between gap-3">
        <div>
          <button
            className="inline-flex items-center gap-2 text-sm text-[var(--muted)] transition hover:text-[var(--text)]"
            onClick={onBack}
            type="button"
          >
            <span aria-hidden="true">←</span>
            Back
          </button>
          <h2 className="mt-4 text-lg font-semibold">Collections</h2>
          <p className="mt-1 text-sm text-[var(--muted)]">
            Create playlists, reorder ROM paths, and manage collection artwork.
          </p>
        </div>
        <button
          className="rounded-md border border-[var(--accent)] bg-[var(--accent)] px-4 py-2 text-sm font-semibold text-black disabled:cursor-not-allowed disabled:opacity-50"
          disabled={!csrf}
          onClick={() => {
            void createCollection();
          }}
          type="button"
        >
          New Collection
        </button>
      </div>

      {notice ? (
        <div className="rounded-xl border border-[var(--border)] bg-[var(--panel)] px-4 py-3 text-sm text-[var(--muted)]">
          {notice}
        </div>
      ) : null}

      <section className="rounded-[24px] border border-[var(--border)] bg-[var(--panel)] px-5 py-4">
        <div className="flex flex-wrap items-center justify-between gap-4">
          <div>
            <h3 className="text-sm font-semibold">Collection Background</h3>
            <p className="mt-1 text-xs text-[var(--muted)]">Stored at `Collections/.media/bg.png`.</p>
          </div>
          <div className="flex items-center gap-2">
            <button
              className="rounded-md border border-[var(--border)] px-3 py-2 text-sm"
              disabled={!csrf}
              onClick={() => {
                void uploadNamedPng("Collections/.media", "bg.png");
              }}
              type="button"
            >
              Upload Background
            </button>
            <button
              className="rounded-md border border-rose-300/30 px-3 py-2 text-sm text-rose-100 disabled:opacity-50"
              disabled={!csrf || !backgroundPath}
              onClick={() => {
                void removePath("Collections/.media/bg.png", "the collections background");
              }}
              type="button"
            >
              Remove
            </button>
          </div>
        </div>
        {backgroundPath ? (
          <img alt="Collections background" className="mt-4 h-20 rounded-lg border border-[var(--border)] object-contain" src={backgroundPath} />
        ) : (
          <p className="mt-4 text-sm text-[var(--muted)]">No collections background uploaded.</p>
        )}
      </section>

      <section className="grid grid-cols-1 gap-4 lg:grid-cols-2">
        {loading ? (
          <div className="rounded-[24px] border border-[var(--border)] bg-[var(--panel)] px-5 py-10 text-center text-sm text-[var(--muted)]">
            Loading collections...
          </div>
        ) : collections.length === 0 ? (
          <div className="rounded-[24px] border border-[var(--border)] bg-[var(--panel)] px-5 py-10 text-center text-sm text-[var(--muted)]">
            No collections found.
          </div>
        ) : (
          collections.map((collection) => {
            const iconUrl = collection.iconPath ? buildDownloadUrl("files", collection.iconPath, undefined, csrf) : null;

            return (
              <article key={collection.fileName} className="rounded-[24px] border border-[var(--border)] bg-[var(--panel)] px-5 py-5">
                <div className="flex items-start justify-between gap-4">
                  <div className="flex items-center gap-4">
                    <div className="flex h-16 w-16 items-center justify-center overflow-hidden rounded-xl border border-[var(--border)] bg-[var(--bg)]">
                      {iconUrl ? (
                        <img alt={`${collection.name} icon`} className="h-full w-full object-contain" src={iconUrl} />
                      ) : (
                        <span className="text-xs uppercase tracking-[0.2em] text-[var(--muted)]">COL</span>
                      )}
                    </div>
                    <div>
                      <h3 className="font-semibold">{collection.name}</h3>
                      <p className="mt-1 text-sm text-[var(--muted)]">{collection.romPaths.length} ROMs</p>
                    </div>
                  </div>
                  <div className="flex flex-wrap justify-end gap-2">
                    <button
                      className="rounded-md border border-[var(--border)] px-3 py-2 text-sm"
                      onClick={() => {
                        setEditor(collection);
                      }}
                      type="button"
                    >
                      Edit
                    </button>
                    <button
                      className="rounded-md border border-[var(--border)] px-3 py-2 text-sm disabled:opacity-50"
                      disabled={!csrf}
                      onClick={() => {
                        void uploadNamedPng("Collections/.media", `${collection.name}.png`);
                      }}
                      type="button"
                    >
                      Upload Icon
                    </button>
                    <button
                      className="rounded-md border border-rose-300/30 px-3 py-2 text-sm text-rose-100 disabled:opacity-50"
                      disabled={!csrf || !iconUrl}
                      onClick={() => {
                        void removePath(`Collections/.media/${collection.name}.png`, `${collection.name}'s icon`);
                      }}
                      type="button"
                    >
                      Remove Icon
                    </button>
                    <button
                      className="rounded-md border border-rose-300/30 px-3 py-2 text-sm text-rose-100 disabled:opacity-50"
                      disabled={!csrf}
                      onClick={() => {
                        void deleteCollection(collection);
                      }}
                      type="button"
                    >
                      Delete
                    </button>
                  </div>
                </div>
                {collection.romPaths.length > 0 ? (
                  <ul className="mt-4 space-y-1 text-sm text-[var(--muted)]">
                    {collection.romPaths.slice(0, 3).map((path) => (
                      <li key={path} className="truncate">
                        {path}
                      </li>
                    ))}
                    {collection.romPaths.length > 3 ? <li>+ {collection.romPaths.length - 3} more</li> : null}
                  </ul>
                ) : (
                  <p className="mt-4 text-sm text-[var(--muted)]">No ROMs assigned yet.</p>
                )}
              </article>
            );
          })
        )}
      </section>

      {editor ? (
        <CollectionEditorModal
          collection={editor}
          csrf={csrf}
          onClose={() => {
            setEditor(null);
          }}
          onSaved={(romPaths) => {
            setCollections((current) =>
              current.map((collection) =>
                collection.fileName === editor.fileName ? { ...collection, romPaths } : collection,
              ),
            );
          }}
        />
      ) : null}
    </div>
  );
}

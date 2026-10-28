import { useCallback, useEffect, useRef } from 'react';
import { createPortal } from 'react-dom';
import { AlertTriangle } from 'lucide-react';
import { useDocumentStore, useIsDirty } from '../stores/documentStore';
import { useSelectionStore } from '../stores/selectionStore';
import { useViewStore } from '../stores/viewStore';
import { NEW_ROADMAP_CHOICES } from '../utils/templates';
import { detachFile } from '../persistence/fileIo';

/**
 * Starting a roadmap of your own (SRS-EDIT-017).
 *
 * The app opened on a worked AUTOSAR programme and there was no way back to a
 * blank one except a template hidden in the command palette, so a new user's
 * first job was deleting someone else's plan row by row (owner 2026-10-27).
 *
 * It is a chooser rather than a plain "New", because both things people mean by
 * it are real: an empty sheet, and a filled-in example to learn the shape from.
 * The example is listed here too, so dismantling it is no longer irreversible.
 */
export function NewRoadmapDialog() {
  const open = useViewStore((s) => s.newRoadmapOpen);
  const setOpen = useViewStore((s) => s.setNewRoadmapOpen);
  const onClose = useCallback(() => setOpen(false), [setOpen]);
  const newDocument = useDocumentStore((s) => s.newDocument);
  const clearSelection = useSelectionStore((s) => s.clear);
  const dirty = useIsDirty();
  const firstRef = useRef<HTMLButtonElement | null>(null);

  useEffect(() => {
    if (!open) return;
    firstRef.current?.focus();
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape') {
        e.stopPropagation();
        onClose();
      }
    };
    window.addEventListener('keydown', onKey, true);
    return () => window.removeEventListener('keydown', onKey, true);
  }, [open, onClose]);

  if (!open) return null;

  const start = (id: string) => {
    const choice = NEW_ROADMAP_CHOICES.find((c) => c.id === id);
    if (!choice) return;
    // Release the previous plan's file FIRST. The binding is module state and
    // outlives the document, so leaving it in place meant the next Ctrl+S
    // wrote this empty roadmap over the plan the user just left.
    detachFile();
    // A deep clone, so editing the new roadmap cannot reach back into the
    // template object every later New would hand out.
    newDocument(JSON.parse(JSON.stringify(choice.build())));
    clearSelection();
    onClose();
  };

  return createPortal(
    <div className="dialog-scrim" onMouseDown={onClose}>
      <div
        className="dialog dialog--wide"
        role="dialog"
        aria-modal="true"
        aria-label="Start a new roadmap"
        onMouseDown={(e) => e.stopPropagation()}
      >
        <h2 className="dialog__title">Start a new roadmap</h2>

        {dirty && (
          <p className="dialog__warn">
            <AlertTriangle size={14} strokeWidth={2} />
            <span>
              This roadmap has changes that are not in a file. Starting a new one replaces it, and
              undo will not bring it back — save it first if you want to keep it.
            </span>
          </p>
        )}

        <div className="dialog__choices">
          {NEW_ROADMAP_CHOICES.map((c, i) => (
            <button
              key={c.id}
              ref={i === 0 ? firstRef : undefined}
              className="dialog__choice"
              onClick={() => start(c.id)}
            >
              <span className="dialog__choice-name">{c.name}</span>
              <span className="dialog__choice-desc">{c.description}</span>
            </button>
          ))}
        </div>

        <div className="dialog__actions">
          <button className="dialog__btn" onClick={onClose}>
            Cancel
          </button>
        </div>
      </div>
    </div>,
    document.body,
  );
}

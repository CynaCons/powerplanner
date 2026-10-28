import { useEffect, useState } from 'react';
import { createPortal } from 'react-dom';
import { X } from 'lucide-react';
import { APP_VERSION } from '../version';
import { useDocumentStore } from '../stores/documentStore';
import { checkForUpdate, performUpdate, type UpdateInfo } from '../utils/updateChecker';
import { getCurrentHandle } from '../persistence/fileIo';
import { notify } from '../stores/noticeStore';

type Status =
  | 'idle'
  | 'checking'
  | 'available'
  | 'up-to-date'
  | 'updating-live'
  | 'updating-download'
  | 'downloaded'
  | 'failed';

/**
 * Settings, with About & updates (PLAN.md v2.17.0).
 *
 * Ported from PowerScroll's settings panel: the app can fetch a newer release
 * and either rewrite the file you have open, or hand you the new one.
 */
export function SettingsDialog({ onClose }: { onClose: () => void }) {
  const doc = useDocumentStore((s) => s.doc);
  const [status, setStatus] = useState<Status>('idle');
  const [info, setInfo] = useState<UpdateInfo | null>(null);
  const [inPlaceFailed, setInPlaceFailed] = useState(false);

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => { if (e.key === 'Escape') onClose(); };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [onClose]);

  const check = async () => {
    setStatus('checking');
    const result = await checkForUpdate(APP_VERSION, { force: true });
    if (result?.available && result.latestVersion) {
      setInfo(result);
      setStatus('available');
    } else if (result) {
      setInfo(result);
      setStatus('up-to-date');
    } else {
      setStatus('failed');
    }
  };

  const update = async () => {
    if (!info?.latestVersion) return;
    // Whether the file can be rewritten in place decides the wording, so it is
    // settled before the work starts rather than guessed afterwards.
    setStatus(getCurrentHandle() ? 'updating-live' : 'updating-download');

    const result = await performUpdate(
      doc,
      APP_VERSION,
      info.latestVersion,
      info.tag,
      info.downloadUrl,
    );

    if (!result.ok) {
      setStatus('failed');
      notify('The update could not be fetched.', 'fail');
      return;
    }
    // live-swap reloads the page, so there is nothing left to show.
    if (result.mode === 'download') {
      setInPlaceFailed(result.inPlaceFailed === true);
      setStatus('downloaded');
      // Through the one live region too, so the report survives closing this
      // dialog — which used to lose it.
      notify(
        result.inPlaceFailed
          ? `Could not rewrite this file. The new version and a backup are both in your downloads.`
          : `Downloaded version ${info.latestVersion}. Open the new file to continue.`,
        result.inPlaceFailed ? 'fail' : 'done',
      );
    }
  };

  return createPortal(
    <div className="settings-scrim" role="presentation" onClick={onClose}>
      <div
        className="settings-dialog"
        role="dialog"
        aria-modal="true"
        aria-label="Settings"
        onClick={(e) => e.stopPropagation()}
      >
        <header className="settings-dialog__head">
          <h2>Settings</h2>
          <button onClick={onClose} aria-label="Close settings" className="settings-dialog__close">
            <X size={16} strokeWidth={1.75} />
          </button>
        </header>

        <section className="settings-section">
          <h3>About &amp; updates</h3>
          <dl className="settings-facts">
            <dt>Version</dt>
            <dd>{APP_VERSION}</dd>
            <dt>This plan</dt>
            <dd>{doc.title || 'Untitled'}</dd>
          </dl>

          <div className="settings-actions">
            <button onClick={check} disabled={status === 'checking'} className="settings-btn">
              {status === 'checking' ? 'Checking…' : 'Check for updates'}
            </button>
            {status === 'available' && (
              <button onClick={update} className="settings-btn settings-btn--primary">
                Update to {info?.latestVersion}
              </button>
            )}
          </div>

          {/* Not a live region: the status bar holds the one announced copy,
              and two polite regions mutating together stall a screen reader. */}
          <p className="settings-note">
            {status === 'up-to-date' && `You are on the newest release (${info?.latestVersion ?? APP_VERSION}).`}
            {status === 'available' && `Version ${info?.latestVersion} is available.`}
            {status === 'updating-live' &&
              'Updating this file in place. A backup is saved first, then the page reloads.'}
            {status === 'updating-download' &&
              'Downloading the new version with this plan inside it.'}
            {status === 'downloaded' && !inPlaceFailed &&
              'Downloaded. Open the new file to continue — the one you have now is untouched.'}
            {status === 'downloaded' && inPlaceFailed &&
              'This file could not be rewritten, so the new version was downloaded instead. A backup of the current one was downloaded first, and the file you have open may not have survived the attempt — open the downloaded copy.'}
            {status === 'failed' && (
              <>
                Could not reach the update service.{' '}
                {info?.releaseUrl && (
                  <a href={info.releaseUrl} target="_blank" rel="noreferrer">Open releases</a>
                )}
              </>
            )}
            {status === 'idle' &&
              'Updating keeps this plan and swaps the app around it. If this file was opened from disk, it is rewritten in place.'}
          </p>
        </section>
      </div>
    </div>,
    document.body,
  );
}

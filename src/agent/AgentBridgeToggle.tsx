import { useState, useCallback, useRef } from 'react';
import { Bot } from 'lucide-react';
import type { AgentBridge } from './AgentBridge';

/**
 * Explicit opt-in control for the agent bridge (SRS-ABR-001).
 *
 * Deliberately NOT auto-connecting: the user grants remote control of their
 * document by pressing this, and the same control revokes it immediately
 * (SRS-ABR-061).
 *
 * The bridge module is imported dynamically so that the production build,
 * where `import.meta.env.DEV` is statically false, drops the import entirely
 * along with everything it reaches (SRS-ABR-050).
 */
export function AgentBridgeToggle() {
  const [state, setState] = useState<'off' | 'connecting' | 'on' | 'error'>('off');
  const [detail, setDetail] = useState<string | null>(null);
  const bridgeRef = useRef<AgentBridge | null>(null);

  const toggle = useCallback(async () => {
    if (bridgeRef.current) {
      bridgeRef.current.stop();
      bridgeRef.current = null;
      setState('off');
      setDetail(null);
      return;
    }

    setState('connecting');
    setDetail(null);
    try {
      const { AgentBridge: Bridge } = await import('./AgentBridge');
      const bridge = new Bridge({
        onStatusChange: (connected) => setState(connected ? 'on' : 'connecting'),
      });
      await bridge.start();
      bridgeRef.current = bridge;
    } catch (err) {
      setState('error');
      setDetail(err instanceof Error ? err.message : String(err));
    }
  }, []);

  const label =
    state === 'on' ? 'Agent on' : state === 'connecting' ? 'Connecting…' : state === 'error' ? 'Agent failed' : 'Agent';

  return (
    <button
      onClick={() => void toggle()}
      title={
        detail ??
        (state === 'on'
          ? 'An agent can edit this document. Click to disconnect.'
          : 'Allow an agent to edit this document (dev only)')
      }
      aria-pressed={state === 'on'}
      className={`app-header__btn label-hideable ${state === 'on' ? 'is-active' : ''}`}
    >
      <Bot size={14} strokeWidth={1.75} />
      <span>{label}</span>
    </button>
  );
}

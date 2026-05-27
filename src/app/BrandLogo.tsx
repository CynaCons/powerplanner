interface BrandLogoProps {
  variant?: 'compact' | 'full';
  /** Pixel height. Compact width matches height; full width = ~5.8× height. */
  size?: number;
}

/**
 * PowerPlanner brand mark.
 * Symbol: three offset Gantt bars on a stepped baseline, in a confident
 * indigo → violet → coral gradient. Wordmark uses the host typography
 * via `currentColor` so it adapts to dark / light / print themes.
 */
export function BrandLogo({ variant = 'full', size = 22 }: BrandLogoProps) {
  if (variant === 'compact') {
    return (
      <svg
        width={size}
        height={size}
        viewBox="0 0 24 24"
        aria-label="PowerPlanner"
        role="img"
        style={{ display: 'block', flexShrink: 0 }}
      >
        <Defs />
        <Symbol />
      </svg>
    );
  }

  // Full lockup: symbol + wordmark.
  // 22px symbol + 6px gap + ~108px wordmark = 136px @ size=22, scales linearly.
  const w = size * (140 / 22);
  return (
    <svg
      width={w}
      height={size}
      viewBox="0 0 140 22"
      aria-label="PowerPlanner"
      role="img"
      style={{ display: 'block', flexShrink: 0 }}
    >
      <Defs />
      <g transform="translate(0 -1)">
        <Symbol />
      </g>
      <text
        x={32}
        y={16}
        fill="currentColor"
        fontFamily="Inter, -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif"
        fontSize={13.5}
        fontWeight={600}
        letterSpacing={-0.35}
      >
        <tspan>Power</tspan>
        <tspan fontWeight={500} fillOpacity={0.78}>
          Planner
        </tspan>
      </text>
    </svg>
  );
}

function Defs() {
  return (
    <defs>
      <linearGradient id="pp-brand-grad" x1="0" y1="0" x2="1" y2="1">
        <stop offset="0%" stopColor="#7c83ff" />
        <stop offset="55%" stopColor="#a78bfa" />
        <stop offset="100%" stopColor="#fb7185" />
      </linearGradient>
      <linearGradient id="pp-brand-glow" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stopColor="rgba(255,255,255,0.18)" />
        <stop offset="100%" stopColor="rgba(255,255,255,0)" />
      </linearGradient>
    </defs>
  );
}

function Symbol() {
  return (
    <g>
      {/* Subtle backing tile so the mark reads on both dark + light surfaces */}
      <rect x={0.5} y={0.5} width={23} height={23} rx={5} fill="url(#pp-brand-grad)" opacity={0.14} />
      {/* Three offset bars suggesting a phased schedule */}
      <rect x={3} y={4.5} width={14} height={3} rx={1} fill="url(#pp-brand-grad)" />
      <rect x={6} y={10.5} width={13} height={3} rx={1} fill="url(#pp-brand-grad)" />
      <rect x={2} y={16.5} width={10} height={3} rx={1} fill="url(#pp-brand-grad)" />
      {/* Inset highlight — the Stripe-style craft signal */}
      <rect x={0.5} y={0.5} width={23} height={23} rx={5} fill="url(#pp-brand-glow)" />
    </g>
  );
}

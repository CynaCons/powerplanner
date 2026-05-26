interface Props {
  fromX: number;
  fromY: number;
  toX: number;
  toY: number;
}

export function ConnectorOverlay({ fromX, fromY, toX, toY }: Props) {
  const midX = (fromX + toX) / 2;
  const path = `M ${fromX} ${fromY} L ${midX} ${fromY} L ${midX} ${toY} L ${toX} ${toY}`;
  return (
    <g pointerEvents="none">
      <path d={path} stroke="var(--color-accent)" strokeWidth={2} strokeDasharray="4 3" fill="none" opacity={0.9} />
      <circle cx={toX} cy={toY} r={4} fill="var(--color-accent)" />
    </g>
  );
}

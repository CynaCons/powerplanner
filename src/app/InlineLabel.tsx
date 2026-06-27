import { useEffect, useRef, useState } from 'react';

interface Props {
  text: string;
  x: number;
  y: number;
  width: number;
  height: number;
  color?: string;
  onCommit: (text: string) => void;
  onCancel: () => void;
}

export function InlineLabel({ text, x, y, width, height, color = 'white', onCommit, onCancel }: Props) {
  const [value, setValue] = useState(text);
  const ref = useRef<HTMLInputElement>(null);

  useEffect(() => {
    ref.current?.focus();
    ref.current?.select();
  }, []);

  return (
    <foreignObject x={x} y={y} width={width} height={height}>
      <input
        ref={ref}
        value={value}
        onChange={(e) => setValue(e.target.value)}
        onBlur={() => onCommit(value)}
        onKeyDown={(e) => {
          if (e.key === 'Enter') onCommit(value);
          else if (e.key === 'Escape') onCancel();
        }}
        style={{
          width: '100%',
          height: '100%',
          background: 'var(--color-inline-editor-bg)',
          border: '1px solid var(--color-inline-editor-border)',
          color,
          padding: '0 6px',
          fontSize: 11,
          outline: 'none',
          borderRadius: 3,
        }}
      />
    </foreignObject>
  );
}

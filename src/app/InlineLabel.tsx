import { useEffect, useRef, useState } from 'react';

interface Props {
  text: string;
  x: number;
  y: number;
  width: number;
  height: number;
  onCommit: (text: string) => void;
  onCancel: () => void;
}

/**
 * The one editor for a name, wherever a name is edited (SR-IXC-16).
 *
 * It used to carry `color = 'white'` as a prop default, and neither caller
 * passed anything else, over a field that is `rgba(255,255,255,0.92)` in three
 * of the four themes. White on white: nothing you typed was visible until
 * Enter handed the value back to the SVG text underneath. It looked right for
 * the first instant only because `select()` below puts the browser's selection
 * highlight behind the glyphs, and the first keystroke clears that — so the
 * name vanished exactly when the user started typing (SRS-EDIT-050, owner
 * report 2026-10-29: "I type and I don't see at all what I'm typing").
 *
 * Colour now lives in `.inline-editor` in the stylesheet, next to the theme
 * tokens it has to agree with, and nothing here can override it. Only geometry
 * — which is per-call-site and in SVG user units — stays inline.
 */
export function InlineLabel({ text, x, y, width, height, onCommit, onCancel }: Props) {
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
        className="inline-editor"
        value={value}
        onChange={(e) => setValue(e.target.value)}
        onBlur={() => onCommit(value)}
        // The chart's own mousedown is delegated from the <svg>, so a press
        // inside the field reached it and started a pan: dragging across a
        // word to select it moved the whole view instead. Stopping it here
        // leaves the field with the ordinary text-selection behaviour the user
        // expects of a text box, and does not disturb the surface.
        onMouseDown={(e) => e.stopPropagation()}
        onDoubleClick={(e) => e.stopPropagation()}
        onKeyDown={(e) => {
          if (e.key === 'Enter') onCommit(value);
          else if (e.key === 'Escape') onCancel();
        }}
        style={{ width: '100%', height: '100%' }}
      />
    </foreignObject>
  );
}

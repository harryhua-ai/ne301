import * as React from 'react';
import { cn } from '@/lib/utils';

export interface SliderProps {
  value?: number;
  defaultValue?: number;
  min?: number;
  max?: number;
  step?: number;
  disabled?: boolean;
  showValue?: boolean;
  className?: string;
  onChange?: (value: number) => void;
  onChangeEnd?: (value: number) => void;
}

export default function Slider({
  value,
  defaultValue,
  min = 0,
  max = 100,
  step = 1,
  disabled,
  showValue = false,
  className,
  onChange,
  onChangeEnd,
}: SliderProps) {
  const isControlled = typeof value === 'number';
  const [inner, setInner] = React.useState<number>(defaultValue ?? 0);
  const current = isControlled ? (value as number) : inner;

  const percentage = React.useMemo(() => {
    if (max === min) return 0;
    return Math.max(0, Math.min(100, ((current - min) / (max - min)) * 100));
  }, [current, min, max]);

  const handleChange = (e: React.ChangeEvent<HTMLInputElement>) => {
    const next = Number((e.target as HTMLInputElement).value);
    if (!isControlled) setInner(next);
    onChange?.(next);
  };

  const handleChangeEnd = () => {
    onChangeEnd?.(current);
  };

  return (
    <div className={cn('flex items-center gap-3', className)}>
      <input
        type="range"
        value={current}
        min={min}
        max={max}
        step={step}
        disabled={disabled}
        onChange={handleChange}
        onMouseUp={handleChangeEnd}
        onTouchEnd={handleChangeEnd}
        className="w-full h-2 bg-gray-200 rounded-lg appearance-none cursor-pointer"
        style={{
          background: `linear-gradient(to right, var(--primary) 0%, var(--primary) ${percentage}%, #e5e7eb ${percentage}%, #e5e7eb 100%)`,
        }}
        aria-label="slider"
      />
      {showValue && (
        <span className="w-10 text-right text-sm text-gray-500">{current}</span>
      )}
    </div>
  );
}

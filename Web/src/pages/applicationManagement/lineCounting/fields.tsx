import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { Switch } from '@/components/ui/switch';

export function Group({ title, children }: { title: string; children: React.ReactNode }) {
    return (
        <div className="border rounded-md p-3 space-y-3">
            <h4 className="text-sm font-semibold text-gray-800">{title}</h4>
            {children}
        </div>
    );
}

export function NumField({
    label, value, min, max, step, onChange,
}: {
    label: string; value: number; min: number; max: number; step: number;
    onChange: (v: number) => void;
}) {
    return (
        <div className="space-y-1">
            <Label className="text-xs">{label}</Label>
            <Input
              type="number"
              value={value}
              min={min}
              max={max}
              step={step}
              onChange={(e) => {
                    const v = Number((e.target as HTMLInputElement).value);
                    if (!Number.isNaN(v)) onChange(v);
                }}
            />
        </div>
    );
}

export function ToggleField({ label, checked, onChange }: { label: string; checked: boolean; onChange: (v: boolean) => void }) {
    return (
        <div className="flex items-center justify-between">
            <Label className="text-xs">{label}</Label>
            <Switch checked={checked} onCheckedChange={onChange} />
        </div>
    );
}

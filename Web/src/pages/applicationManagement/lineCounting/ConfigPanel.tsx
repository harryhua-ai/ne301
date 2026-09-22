import { useLingui } from '@lingui/react';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { Button } from '@/components/ui/button';
import { Switch } from '@/components/ui/switch';
import type { LineCountingConfig, LineCountingStatus } from '@/services/api/line-counting';

export interface ConfigPanelProps {
    config: LineCountingConfig;
    status: LineCountingStatus | null;
    saving: boolean;
    editMode: boolean;
    editPhase: 0 | 1 | 2;
    hasLine: boolean;
    onChange: (patch: Partial<LineCountingConfig>) => void;
    onSave: () => void;
    onToggleEdit: () => void;
    onResetLine: () => void;
    onFlipDirection: () => void;
    onReset: () => void;
}

function Group({ title, children }: { title: string; children: React.ReactNode }) {
    return (
        <div className="border rounded-md p-3 space-y-3">
            <h4 className="text-sm font-semibold text-gray-800">{title}</h4>
            {children}
        </div>
    );
}

function NumField({
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

function ToggleField({ label, checked, onChange }: { label: string; checked: boolean; onChange: (v: boolean) => void }) {
    return (
        <div className="flex items-center justify-between">
            <Label className="text-xs">{label}</Label>
            <Switch checked={checked} onCheckedChange={onChange} />
        </div>
    );
}

export default function ConfigPanel({
    config, status, saving, editMode, editPhase, hasLine,
    onChange, onSave, onToggleEdit, onResetLine, onFlipDirection, onReset,
}: ConfigPanelProps) {
    const { i18n } = useLingui();
    const classes = status?.model.classes ?? [];
    const modelLoaded = !!status?.model.name;

    return (
      <div className="space-y-3" data-testid="lc-config-panel">
            <Group title={i18n._('sys.line_counting.base_group')}>
                <div className="flex items-center justify-between">
                    <Label className="text-xs">{i18n._('sys.line_counting.enable')}</Label>
                    <Switch
                      checked={config.enable}
                      onCheckedChange={(v) => onChange({ enable: v })}
                    />
                </div>
                <div className="space-y-1">
                    <Label className="text-xs">{i18n._('sys.line_counting.counter_name')}</Label>
                    <Input
                      value={config.counter_name}
                      onChange={(e) => onChange({ counter_name: (e.target as HTMLInputElement).value })}
                    />
                </div>
                <div className="space-y-1">
                    <Label className="text-xs">{i18n._('sys.line_counting.target_class')}</Label>
                    <select
                      className="w-full h-9 rounded-md border border-gray-300 bg-white px-2 text-sm"
                      value={config.target_class}
                      onChange={(e) => onChange({ target_class: (e.target as HTMLSelectElement).value })}
                    >
                        {classes.length === 0 && <option value={config.target_class}>{config.target_class}</option>}
                        {classes.map((c) => (
                            <option key={c.id} value={c.name}>{c.name}</option>
                        ))}
                    </select>
                    <p className="text-[11px] text-gray-400">
                        {modelLoaded
                            ? i18n._('sys.line_counting.current_model', { name: status?.model.name, version: status?.model.version })
                            : i18n._('sys.line_counting.current_model_unknown')}
                    </p>
                </div>
            </Group>

            <Group title={i18n._('sys.line_counting.line_group')}>
                <div className="flex flex-wrap gap-2">
                    <Button size="sm" variant={editMode ? 'default' : 'outline'} onClick={onToggleEdit}>
                        {editMode ? i18n._('sys.line_counting.finish_draw') : i18n._('sys.line_counting.draw_line')}
                    </Button>
                    <Button size="sm" variant="outline" onClick={onFlipDirection} disabled={!hasLine}>
                        {i18n._('sys.line_counting.flip_direction')}
                    </Button>
                    <Button size="sm" variant="outline" onClick={onResetLine}>
                        {i18n._('sys.line_counting.reset_line')}
                    </Button>
                </div>
                {editMode && (
                    <p className="text-[11px] text-amber-600">
                        {editPhase === 0 ? i18n._('sys.line_counting.hint_start') : editPhase === 1 ? i18n._('sys.line_counting.hint_end') : i18n._('sys.line_counting.hint_draft')}
                    </p>
                )}
            </Group>

            <Group title={i18n._('sys.line_counting.tracking_group')}>
                <NumField
                  label={i18n._('sys.line_counting.conf_threshold')}
                  value={config.confidence_threshold}
                  min={0}
                  max={1}
                  step={0.05}
                  onChange={(v) => onChange({ confidence_threshold: v })}
                />
                <NumField
                  label={i18n._('sys.line_counting.assoc_distance')}
                  value={config.tracking.association_distance}
                  min={0}
                  max={1}
                  step={0.05}
                  onChange={(v) => onChange({ tracking: { ...config.tracking, association_distance: v } })}
                />
                <div className="grid grid-cols-3 gap-2">
                    <NumField
                      label={i18n._('sys.line_counting.track_history')}
                      value={config.tracking.history_length}
                      min={4}
                      max={16}
                      step={1}
                      onChange={(v) => onChange({ tracking: { ...config.tracking, history_length: v } })}
                    />
                    <NumField
                      label={i18n._('sys.line_counting.max_missed')}
                      value={config.tracking.max_missed_frames}
                      min={1}
                      max={60}
                      step={1}
                      onChange={(v) => onChange({ tracking: { ...config.tracking, max_missed_frames: v } })}
                    />
                    <NumField
                      label={i18n._('sys.line_counting.confirm_frames')}
                      value={config.tracking.confirmation_frames}
                      min={1}
                      max={16}
                      step={1}
                      onChange={(v) => onChange({ tracking: { ...config.tracking, confirmation_frames: v } })}
                    />
                </div>
            </Group>

            <Group title={i18n._('sys.line_counting.reporting_group')}>
                <NumField
                  label={i18n._('sys.line_counting.window_minutes')}
                  value={config.window_minutes}
                  min={1}
                  max={1440}
                  step={1}
                  onChange={(v) => onChange({ window_minutes: v })}
                />
                <ToggleField
                  label={i18n._('sys.line_counting.mqtt_report')}
                  checked={config.reporting.mqtt_enabled}
                  onChange={(v) => onChange({ reporting: { ...config.reporting, mqtt_enabled: v } })}
                />
                <ToggleField
                  label={i18n._('sys.line_counting.webhook_report')}
                  checked={config.reporting.webhook_enabled}
                  onChange={(v) => onChange({ reporting: { ...config.reporting, webhook_enabled: v } })}
                />
                <ToggleField
                  label={i18n._('sys.line_counting.tracks_report')}
                  checked={config.reporting.tracks_enabled}
                  onChange={(v) => onChange({ reporting: { ...config.reporting, tracks_enabled: v } })}
                />
                <ToggleField
                  label={i18n._('sys.line_counting.heat_report')}
                  checked={config.reporting.heat_grid_enabled}
                  onChange={(v) => onChange({ reporting: { ...config.reporting, heat_grid_enabled: v } })}
                />
                <NumField
                  label={i18n._('sys.line_counting.backlog_capacity')}
                  value={config.reporting.backlog_capacity}
                  min={1}
                  max={256}
                  step={1}
                  onChange={(v) => onChange({ reporting: { ...config.reporting, backlog_capacity: v } })}
                />
                <p className="text-[11px] text-gray-400">{i18n._('sys.line_counting.transport_hint')}</p>
            </Group>

            <div className="sticky bottom-0 bg-white pt-2 pb-1 flex gap-2">
                <Button className="flex-1" onClick={onSave} disabled={saving}>
                    {saving ? i18n._('sys.line_counting.saving') : i18n._('common.save')}
                </Button>
                <Button className="flex-1" variant="outline" onClick={onReset}>
                    {i18n._('sys.line_counting.reset_stats')}
                </Button>
            </div>
      </div>
    );
}

import { useLingui } from '@lingui/react';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { Switch } from '@/components/ui/switch';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select';
import type { LineCountingConfig, LineCountingStatus } from '@/services/api/line-counting';
import { Group, NumField } from './fields';

export interface ConfigPageProps {
    config: LineCountingConfig;
    status: LineCountingStatus | null;
    onChange: (patch: Partial<LineCountingConfig>) => void;
}

export default function ConfigPage({ config, status, onChange }: ConfigPageProps) {
    const { i18n } = useLingui();
    const classes = status?.model.classes ?? [];
    const modelLoaded = !!status?.model.name;

    return (
      <div className="space-y-3" data-testid="lc-page-config">
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
                    <Select
                      value={config.target_class}
                      onValueChange={(v) => onChange({ target_class: v })}
                    >
                        <SelectTrigger className="w-full">
                            <SelectValue placeholder={config.target_class} />
                        </SelectTrigger>
                        <SelectContent>
                            {classes.length === 0 && <SelectItem value={config.target_class}>{config.target_class}</SelectItem>}
                            {classes.map((c) => (
                                <SelectItem key={c.id} value={c.name}>{c.name}</SelectItem>
                            ))}
                        </SelectContent>
                    </Select>
                    <p className="text-[11px] text-gray-400" data-testid="lc-current-model">
                        {modelLoaded
                            ? i18n._('sys.line_counting.current_model').replace('{name}', status?.model.name ?? '')
                            : i18n._('sys.line_counting.current_model_unknown')}
                    </p>
                </div>
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
                  min={0.05}
                  max={1}
                  step={0.05}
                  onChange={(v) => onChange({ tracking: { ...config.tracking, association_distance: v } })}
                />
                <div className="grid grid-cols-1 sm:grid-cols-3 gap-2">
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
      </div>
    );
}

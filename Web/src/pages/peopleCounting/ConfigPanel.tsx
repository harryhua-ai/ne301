import { useLingui } from '@lingui/react';
import { Card, CardContent } from '@/components/ui/card';
import { Label } from '@/components/ui/label';
import { Input } from '@/components/ui/input';
import { Switch } from '@/components/ui/switch';
import { Button } from '@/components/ui/button';
import type { PeopleCountingConfig } from '@/services/api/peopleCounting';

export interface ConfigPanelProps {
    config: PeopleCountingConfig;
    onChange: (patch: Partial<PeopleCountingConfig>) => void;
    onSave: () => void;
    saving: boolean;
    editMode: boolean;
    editPhase: 0 | 1 | 2;
    hasLine: boolean;
    onToggleEdit: () => void;
    onResetLine: () => void;
    onFlipDirection: () => void;
}

/** Helper: permille → percent display string */
function permilleToPercent(p: number): string {
    return `${(p / 10).toFixed(1)}%`;
}

export default function ConfigPanel({ config, onChange, onSave, saving, editMode, editPhase, hasLine, onToggleEdit, onResetLine, onFlipDirection }: ConfigPanelProps) {
    const { i18n } = useLingui();

    const phaseHintKey = !editMode ? null
        : editPhase === 0 ? 'sys.pc.hint_click_l1'
        : editPhase === 1 ? 'sys.pc.hint_click_l2'
        : 'sys.pc.hint_done';

    const numField = (key: keyof PeopleCountingConfig, labelKey: string) => (
        <div className="flex flex-col gap-1">
            <Label>{i18n._(labelKey)}</Label>
            <Input
              type="number"
              value={config[key] as number}
              onChange={(e) => onChange({ [key]: Number((e.target as HTMLInputElement).value) } as Partial<PeopleCountingConfig>)}
            />
        </div>
    );

    const permilleField = (key: keyof PeopleCountingConfig, labelKey: string) => (
        <div className="flex flex-col gap-1">
            <Label>{i18n._(labelKey)}</Label>
            <Input
              type="number"
              min={0}
              max={1000}
              value={config[key] as number}
              onChange={(e) => onChange({ [key]: Number((e.target as HTMLInputElement).value) } as Partial<PeopleCountingConfig>)}
            />
            <span className="text-xs text-gray-400">{permilleToPercent(config[key] as number)}</span>
        </div>
    );

    const switchField = (key: keyof PeopleCountingConfig, labelKey: string) => (
        <div className="flex items-center justify-between">
            <Label>{i18n._(labelKey)}</Label>
            <Switch
              checked={config[key] as boolean}
              onCheckedChange={(v) => onChange({ [key]: v } as Partial<PeopleCountingConfig>)}
            />
        </div>
    );

    return (
        <Card>
            <CardContent className="space-y-5 p-4">
                {/* General */}
                <div className="space-y-2.5">
                    <h4 className="text-sm font-semibold text-gray-800">{i18n._('sys.pc.group_general')}</h4>
                    {switchField('enable', 'sys.pc.enable')}
                </div>

                {/* Counting Line */}
                <div className="space-y-2.5">
                    <h4 className="text-sm font-semibold text-gray-800">{i18n._('sys.pc.group_line')}</h4>
                    {/* Draw controls — drive the line overlay on the left video */}
                    <div className="flex flex-wrap items-center gap-3">
                        <Button
                          size="sm"
                          variant={editMode ? 'primary' : 'outline'}
                          onClick={onToggleEdit}
                        >
                            {editMode ? i18n._('sys.pc.edit_done') : i18n._('sys.pc.edit_line')}
                        </Button>
                        <Button size="sm" variant="outline" onClick={onFlipDirection} disabled={!hasLine}>
                            {i18n._('sys.pc.flip_direction')}
                        </Button>
                        <Button size="sm" variant="outline" onClick={onResetLine}>
                            {i18n._('sys.pc.reset_line')}
                        </Button>
                    </div>
                    {phaseHintKey && (
                        <p className="text-xs text-amber-600 leading-snug">{i18n._(phaseHintKey)}</p>
                    )}
                    <div className="grid grid-cols-3 gap-3">
                        {permilleField('line_x1_permille', 'sys.pc.line_x1')}
                        {permilleField('line_y1_permille', 'sys.pc.line_y1')}
                        {permilleField('line_x2_permille', 'sys.pc.line_x2')}
                        {permilleField('line_y2_permille', 'sys.pc.line_y2')}
                        {permilleField('outside_x_permille', 'sys.pc.outside_x')}
                        {permilleField('outside_y_permille', 'sys.pc.outside_y')}
                    </div>
                </div>

                {/* Detection */}
                <div className="space-y-2.5">
                    <h4 className="text-sm font-semibold text-gray-800">{i18n._('sys.pc.group_detection')}</h4>
                    <div className="grid grid-cols-2 gap-3">
                        {permilleField('conf_threshold_permille', 'sys.pc.conf_threshold')}
                        {permilleField('max_dist_permille', 'sys.pc.max_dist')}
                        <div className="flex flex-col gap-1">
                            <Label>{i18n._('sys.pc.target_class')}</Label>
                            <Input
                              type="text"
                              value={config.target_class_name}
                              onChange={(e) => onChange({ target_class_name: (e.target as HTMLInputElement).value })}
                            />
                        </div>
                        <div className="flex flex-col gap-1">
                            <Label>{i18n._('sys.pc.model_name')}</Label>
                            <Input
                              type="text"
                              value={config.model_name}
                              onChange={(e) => onChange({ model_name: (e.target as HTMLInputElement).value })}
                            />
                        </div>
                    </div>
                </div>

                {/* Tracking */}
                <div className="space-y-2.5">
                    <h4 className="text-sm font-semibold text-gray-800">{i18n._('sys.pc.group_tracking')}</h4>
                    <div className="grid grid-cols-3 gap-3">
                        {numField('track_history_k', 'sys.pc.track_history_k')}
                        {numField('max_miss', 'sys.pc.max_miss')}
                        {numField('k_confirm', 'sys.pc.k_confirm')}
                    </div>
                </div>

                {/* Reporting */}
                <div className="space-y-2.5">
                    <h4 className="text-sm font-semibold text-gray-800">{i18n._('sys.pc.group_reporting')}</h4>
                    <div className="space-y-1">
                        <div className="grid grid-cols-2 gap-3">
                            {numField('window_minutes', 'sys.pc.window_minutes')}
                            {numField('backlog_capacity', 'sys.pc.backlog_capacity')}
                        </div>
                        {switchField('mqtt_report_enable', 'sys.pc.mqtt_report')}
                        {switchField('webhook_report_enable', 'sys.pc.webhook_report')}
                    </div>
                </div>

                <Button variant="primary" size="sm" className="w-full mt-2" onClick={onSave} disabled={saving}>
                    {saving ? i18n._('sys.pc.saving') : i18n._('sys.pc.save')}
                </Button>
            </CardContent>
        </Card>
    );
}

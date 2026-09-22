import { useLingui } from '@lingui/react';
import type { LineCountingConfig } from '@/services/api/line-counting';
import { Group, NumField, ToggleField } from './fields';

export interface AdvancedPageProps {
    config: LineCountingConfig;
    onChange: (patch: Partial<LineCountingConfig>) => void;
}

export default function AdvancedPage({ config, onChange }: AdvancedPageProps) {
    const { i18n } = useLingui();

    return (
      <div className="space-y-3" data-testid="lc-page-advanced">
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
      </div>
    );
}

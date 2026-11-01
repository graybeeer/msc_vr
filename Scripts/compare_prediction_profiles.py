"""Compare rendered prediction-rate captures without combining nested timings.

python Scripts/compare_prediction_profiles.py <profile.json> ... --out report.json
Uses the phase boundaries recorded by profile_periodic_hitches_pie.py and the
native CSV FrameTime, CPU/GPU scopes and prediction/sensor counters. Startup
and the observer transition are excluded, never arbitrary slow frames.
"""
import argparse
import json
from pathlib import Path
from analyze_warehouse_profile import read_capture, number, summary, leaf


def analyze_profile(path):
    report = json.loads(path.read_text(encoding='utf-8-sig'))
    if not report.get('passed'):
        raise ValueError('Profile did not complete: ' + str(path))
    header, records, metadata, originals = read_capture(Path(report['csv']))
    if metadata.get('HasHeaderRowAtEnd') != '1':
        raise ValueError('Native CSV writer did not finish')
    find = lambda tail: next(i for i, h in enumerate(header)
                             if leaf(originals[h]).lower() == tail.lower() and not h.startswith('COUNTS/'))
    frame_i, sim_i = find('FrameTime'), find('SimulationSeconds')
    wanted = {'FrameTime', 'GameThreadTime', 'RenderThreadTime', 'RHIThreadTime', 'GPUTime',
              'LocalizationTick', 'MotionPrediction', 'LocalizationScanMatch', 'SensorSnapshot',
              'RoutePlanning', 'LoadGraph', 'ForkliftMotors', 'ObserverVisibility', 'ObserverWallFade'}
    selected = {h: i for i, h in enumerate(header) if leaf(originals[h]) in wanted and not h.startswith('COUNTS/')}
    counters = {h: i for i, h in enumerate(header) if leaf(originals[h]) in
                {'PredictionUpdates', 'SensorSnapshots', 'SensorBeams', 'RoutePlans', 'LoadGraphBuilds'}
                and not h.startswith('COUNTS/')}
    rows = []
    for _, raw in records:
        values = [number(raw[i]) if i < len(raw) else None for i in range(len(header))]
        if values[frame_i] and values[frame_i] > 0 and values[sim_i] is not None:
            rows.append(values)
    phases = {}
    for index, phase in enumerate(report['phases'][:-1]):
        if phase['name'] not in ('first_person', 'observer_floor0'):
            continue
        begin = phase['simulation_seconds']
        end = report['phases'][index + 1]['simulation_seconds']
        subset = [v for v in rows if begin <= v[sim_i] < end]
        if not subset:
            raise ValueError('No frames in phase ' + phase['name'])
        times = [v[frame_i] for v in subset]
        timings = {h: summary([v[i] or 0 for v in subset]) for h, i in selected.items()}
        totals = {h: sum(v[i] or 0 for v in subset) for h, i in counters.items()}
        counts = {leaf(originals[h]): value for h, value in totals.items()}
        per_prediction = {}
        for h, i in selected.items():
            if leaf(originals[h]) == 'MotionPrediction' and counts.get('PredictionUpdates'):
                per_prediction[h] = sum(v[i] or 0 for v in subset) / counts['PredictionUpdates']
        phases[phase['name']] = dict(simulation_range_s=[begin, end], frames=len(times),
            frame_ms=summary(times), fps_from_mean_frame=1000 / (sum(times) / len(times)),
            frames_over_ms={str(t): sum(v > t for v in times) for t in (33.333, 50, 100, 1000)},
            timings_ms=timings, counters=totals, motion_ms_per_prediction=per_prediction,
            mean_prediction_hz_per_vehicle=counts.get('PredictionUpdates', 0) / (end - begin) / len(report['vehicles']),
            mean_sensor_hz_per_vehicle=counts.get('SensorSnapshots', 0) / (end - begin) / len(report['vehicles']))
    prediction_windows = [r['window_s'] for r in report.get('prediction_updates', [])
                          if r['phase'] in phases and r['window_s'] > 0]
    return dict(label=report['label'], requested_prediction_hz=report.get('prediction_hz'),
        source=str(path.resolve()), phases=phases, prediction_window_s=summary(prediction_windows),
        vehicles=report['vehicles'], cargo=report['cargo'], pallets=report['pallets'], actors=report['actors'],
        metadata=metadata, workload_samples=report['samples'],
        notes=['Native timing units are ms; inclusive scopes overlap and must not be summed.',
               'FrameTime includes pacing/waits. A profile is not a completed transport test.',
               'The world is fixed at 30 Hz; actual prediction Hz is capped by world frames.',
               'Phase boundaries and native FrameTime can differ by one frame.',
               'The same start command is used; changed task progress can change planning/render workload.',
               'All-zero GPU/native columns may be unavailable; they do not prove zero cost.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('profiles', nargs='+', type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    results = [analyze_profile(p) for p in args.profiles]
    args.out.write_text(json.dumps(dict(captures=results), ensure_ascii=False, indent=2), encoding='utf-8')
    for r in results:
        for name, phase in r['phases'].items():
            stat = phase['frame_ms']
            print(r['label'], name, 'target_hz', r['requested_prediction_hz'],
                  'fps', round(phase['fps_from_mean_frame'], 2), 'p95_ms', round(stat['p95'], 2),
                  'p99_ms', round(stat['p99'], 2), 'max_ms', round(stat['max'], 2),
                  'prediction_hz', round(phase['mean_prediction_hz_per_vehicle'], 2))


if __name__ == '__main__':
    main()

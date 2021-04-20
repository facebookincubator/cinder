# random,1310720,google_dense_hash_map,45621248,0.344362020493
# random,2621440,glib_hash_table,109867008,1.01163601875
# random,2621440,stl_unordered_map,130715648,1.73484396935
# random,2621440,boost_unordered_map,108380160,1.11585187912
# random,2621440,google_sparse_hash_map,37015552,1.76031804085
# random,2621440,google_dense_hash_map,79175680,0.504401922226
# random,5242880,glib_hash_table,210530304,1.86031603813
# random,5242880,stl_unordered_map,250298368,3.81597208977
# random,5242880,boost_unordered_map,192184320,2.63760495186
# random,5242880,google_sparse_hash_map,62066688,3.93570995331
# random,5242880,google_dense_hash_map,146284544,1.22620105743
# random,10485760,glib_hash_table,411856896,4.16937494278
# random,10485760,stl_unordered_map,490430464,7.91806197166
# random,10485760,boost_unordered_map,359251968,7.52085900307
# random,10485760,google_sparse_hash_map,111902720,8.11318516731
# random,10485760,google_dense_hash_map,280502272,2.32930994034
# random,20971520,glib_hash_table,814510080,8.32456207275
# random,20971520,stl_unordered_map,971583488,16.1606841087
# random,20971520,boost_unordered_map,692441088,24.5845990181
# random,20971520,google_sparse_hash_map,211435520,16.2772600651
# random,20971520,google_dense_hash_map,548937728,4.85360789299
# random,41943040,glib_hash_table,1619816448,90.6313672066

import sys, json

lines = [ line.strip() for line in sys.stdin if line.strip() ]

by_benchtype = {}
benches = {}
programs = {}

for line in lines:
    benchtype, type, nkeys, program, value = line.split(',')
    nkeys = int(nkeys)
    programs[program] = 1
    
    if (type == 'time'):
        by_benchtype.setdefault("%s-runtime" % benchtype, {}).setdefault(program, []).append([nkeys, float(value)])
    else:
        by_benchtype.setdefault("%s-memory"  % benchtype, {}).setdefault(program, []).append([nkeys, int(value)])
    benches[benchtype] = 1

proper_names = {
    'std::unordered_map':             'std::unordered_map (1 thread)',
    'spp::sparse_hash_map':           'sparsepp (1 thread, use_spp_alloc)',
    'absl::flat_hash_map':            'absl::flat_hash_map (1 thread)',
    'phmap::flat_hash_map':            'phmap::flat_hash_map',
    'phmap::parallel_flat_hash_map':   'phmap::parallel_flat_hash_map (1 thread)',
    'phmap::parallel_flat_hash_map_mt':   'phmap::parallel_flat_hash_map (8 thread)',
    'absl::parallel_flat_hash_map':   'absl::parallel_flat_hash_map (1 thread)',
    'absl::parallel_flat_hash_map_mt':  'absl::parallel_flat_hash_map (8 threads)',
    'phmap::parallel_flat_hash_map_4':  'phmap::parallel_flat_hash_map (N=4, 8 threads)',
    'phmap::parallel_flat_hash_map_5':  'phmap::parallel_flat_hash_map (N=5, 8 threads)',
    'phmap::parallel_flat_hash_map_6':  'phmap::parallel_flat_hash_map (N=6, 8 threads)'
}

proper_color = {
    'std::unordered_map':               0,
    'spp::sparse_hash_map':             0,
    'absl::flat_hash_map':              1,
    'phmap::flat_hash_map':             1,
    'phmap::parallel_flat_hash_map':    2,
    'phmap::parallel_flat_hash_map_mt': 2,
    'absl::parallel_flat_hash_map':     3,
    'absl::parallel_flat_hash_map_mt':  3,
    'phmap::parallel_flat_hash_map_4':   3,
    'phmap::parallel_flat_hash_map_5':   4,
    'phmap::parallel_flat_hash_map_6':   5
}

bench_titles = {
    'lookup':             'Random Lookup',
    'sequential' :        'Sequential Insert',
    'random' :            'Random Insert',
    'delete' :            'Deletion',
    'sequentialstring' :  'Sequential String Insert',
    'randomstring' :      'Random String Insert',
    'deletestring' :      'String Deletion'
    }

# do them in the desired order to make the legend not overlap the chart data
# too much
program_slugs = [
    'std::unordered_map',
    'sparsepp',
    'absl::flat_hash_map',
    'phmap::flat_hash_map',
    'absl::parallel_flat_hash_map',
    'phmap::parallel_flat_hash_map',
    'phmap::parallel_flat_hash_map_mt',
    'absl::parallel_flat_hash_map_mt',
    'phmap::parallel_flat_hash_map_4',
    'phmap::parallel_flat_hash_map_5',
    'phmap::parallel_flat_hash_map_6'
]

chart_data = {}

for i, (benchtype, programs) in enumerate(by_benchtype.items()):
    chart_data[benchtype] = []
    k = programs.keys()
    k.sort()
    for program in k:
        data = programs.get(program, [])
        chart_data[benchtype].append({
            'label': proper_names[program],
            'color': proper_color[program],
            'data': [],
        })

        for k, (nkeys, value) in enumerate(data):
            chart_data[benchtype][-1]['data'].append([nkeys, value])

html_chart_data = 'chart_data = ' + json.dumps(chart_data)

## print chart_data['delete-runtime']

html_plot_spec = ''
for b in benches.keys():
    html_plot_spec += """
        $.plot($("#{0}-runtime"), chart_data['{0}-runtime'], runtime_settings);
        $.plot($("#{0}-memory"),  chart_data['{0}-memory'],  memory_settings);""".format(b)

html_div_spec = ''
first = 1

for b in benches.keys():
    if 1:
        first = 0
        html_div_spec += """
    <div class="table-title">{1} (integers): Memory Usage</div>
    <div class="chart" id="{0}-memory"></div>
    <div class="xaxis-title">number of entries in hash table</div>
""".format(b, bench_titles[b])

    html_div_spec += """
    <div class="table-title">{1} (integers): Execution Time </div>
    <div class="chart" id="{0}-runtime"></div>
    <div class="xaxis-title">number of entries in hash table</div>

""".format(b, bench_titles[b])



html_template = file('charts-template.html', 'r').read()

html_template = html_template.replace('__CHART_DATA_GOES_HERE__', html_chart_data)
html_template = html_template.replace('__PLOT_SPEC_GOES_HERE__', html_plot_spec)
html_template = html_template.replace('__PLOT_DIV_SPEC_GOES_HERE__', html_div_spec)

file('charts.html', 'w').write(html_template)

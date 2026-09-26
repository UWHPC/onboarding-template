param(
  [int]$n_runs = 20
)

# rebuild benchmark executable
cmake --build --preset benchmark

# validate code correctness
ctest --preset benchmark --output-on-failure

$run_data = @()

for ($i = 1; $i -le $n_runs; $i++) {
  $json = .\build\benchmark\uwhpc_benchmark.exe
  Write-Output $json
  # parse to powershell object
  $data = $json | ConvertFrom-Json
  $run_data += $data
}

function Get-Median {
    param([double[]]$Numbers)
    $sorted = $Numbers | Sort-Object
    $count = $sorted.Count
    if ($count % 2 -eq 0) {
        ($sorted[$count/2 - 1] + $sorted[$count/2]) / 2
    } else {
        $sorted[[math]::Floor($count/2)]
    }
}

$scores = $run_data | Select-Object -ExpandProperty score
$median = Get-Median -Numbers $scores
Write-Output "Median score: $median"

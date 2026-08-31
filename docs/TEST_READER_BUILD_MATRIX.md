# DMC Test Reader — module combinations

| Family | Shared modules | Family modules | Render status |
| --- | --- | --- | --- |
| MOD | identity-probe, bounded-read-guard, shared-model-header, object-table, mesh-table, vertex-stream, topology | mod-adapter | real mesh preview |
| SCM | identity-probe, bounded-read-guard, shared-model-header, object-table, mesh-table, vertex-stream, topology | scm-adapter | real mesh preview |
| EFM | identity-probe, bounded-read-guard, shared-model-envelope | efm-family-adapter; efm-vertex-stream-binding TODO; efm-material-binding TODO; efm-topology-binding TODO | inspection/partial pipeline |
| MRP | identity-probe, bounded-read-guard, shared-model-envelope | mrp-render-family-adapter; mrp-record-schema TODO; mrp-downstream-owner-binding TODO | inspection/partial pipeline |
| SHW | identity-probe, bounded-read-guard, shared-model-envelope | shw-shadow-family-adapter; shw-triangle-index-binding TODO; shw-external-spatial-pool-binding TODO | inspection/partial pipeline |

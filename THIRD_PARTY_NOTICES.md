# Third-party notices

Maremba's original source code is covered by the project's `LICENSE` file.
The material below is not relicensed by Maremba.

## SADIE II Database, D1 (KU100)

`Source/DSP/MeasuredCloseHrtf.h` holds head-related impulse responses modified
from the SADIE II D1 (KU100) measurements, Copyright 2018 University of York,
licensed under the Apache License, Version 2.0.

- Dataset: <https://doi.org/10.5281/zenodo.12092466>
- Licence: [`ThirdParty/SADIE-II-LICENSE.txt`](ThirdParty/SADIE-II-LICENSE.txt)
- Notice and description of the modifications:
  [`ThirdParty/SADIE-II-NOTICE.txt`](ThirdParty/SADIE-II-NOTICE.txt)
- Generation receipt (source hash, processing, error bounds):
  [`Docs/validation/capture-20260927/close-hrtf-generation.json`](Docs/validation/capture-20260927/close-hrtf-generation.json),
  checked by `Tests/generate_close_hrtf.py --check`

Every distributed build that contains the DSP must carry both files.

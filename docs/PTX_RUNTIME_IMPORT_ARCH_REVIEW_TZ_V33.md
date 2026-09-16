# PTX Runtime Architecture Review — AI ТЗ v33

Goal: approve or reject the Reader-only import architecture before copying reverse code.

Review inputs:
- owner authorization;
- read-only source snapshot `50d070e...`;
- current Native Reader PTX pipeline;
- C++23/Spider/size/dedup/exception contracts.

Required review output:
- exact minimum source inventory to copy;
- files/types/functions to re-express in Reader C++23;
- explicit list of reverse-only material not to copy;
- wrapper API decision;
- lazy integration decision;
- test matrix corrections;
- weight-risk notes;
- explicit GO/NO-GO;
- updated implementation task.

#!/usr/bin/env bash

set -u

function addr2func() {
  if [[ $1 =~ ^(-h|-{1,2}help)$ ]] || [[ $# -le 1 ]]; then
    echo "$0 "'elf_filepath [hex style addrs...]'
    return 1
  fi

  local tmpfile=$(mktemp)
  local OBJDUMP=${OBJDUMP:-objdump}
  local elf_filepath=$1
  shift
  $OBJDUMP -d --prefix-addresses -l "$elf_filepath" \
    | awk '/^_.*:$/{file=""} /^\// { file=$1 } /^[0-9a-fA-F]+ / {printf "%s %s", $1, $2; if (file!=""){ printf ":%s", file}; printf "\n"; }' \
      >"$tmpfile"
  local output=($(
    {
      for arg in "$@"; do
        local orig_addr=$arg
        local addr=${arg##0x}
        cat "$tmpfile" \
          | grep -E "^[0 ]*$addr" \
          | awk '{objdump_addr=$1; func_name=$2; printf("%s %s\n", "'"$orig_addr"'", func_name); }'
      done
    } | awk 'NF'
  ))
  if [[ -p /dev/stdin ]]; then
    perl -ne 'BEGIN { @src_pts=(); @dst_pts=(); $pts_len=$#ARGV / 2; while($#ARGV > 0){ push(@src_pts, shift); push(@dst_pts, shift); }; } for ($i = 0; $i < $pts_len; $i++){ s/@src_pts[$i]/@dst_pts[$i]/; } print $_' "${output[@]}"
  else
    printf '%s ' "${output[@]}" | tr ' ' '\n' | awk '{if (NR>1 && NR%2==1) {printf "\n";} printf "%s", $0; if (NR%2!=0) {printf " ";}}'
  fi
}

function help() {
  echo "$0 "'elf_filepath [-offset-1] [-o output]'
}

function main() {
  if [[ $1 =~ ^(-h|-{1,2}help)$ ]] || [[ $# == 0 ]]; then
    help
    return 1
  fi

  local elf_filepath=$1
  local offset_minux_one_flag=0
  local output_file="output.json"
  shift

  if [[ $# -ge 1 ]]; then
    if [[ $1 == '-offset-1' ]]; then
      offset_minux_one_flag=1
      shift
    fi
  fi

  if [[ $# -ge 2 ]]; then
    if [[ $1 == '-o' ]]; then
      output_file=$2
      shift
      shift
    fi
  fi
  if [[ $# != 0 ]]; then
    help
    return 1
  fi

  echo 1>&2 "[start concat trace files]"
  if [[ $offset_minux_one_flag == 0 ]]; then
    # hex addre 0 offset
    cat ./iftracer.out.* >./iftracer-tmp.out
  else
    # hex addre -1 offset
    cat ./iftracer.out.* | sed -E -e 's/1$/0/' -e 's/5$/4/' -e 's/9$/8/' -e 's/d$/c/' >./iftracer-tmp.out
  fi

  echo 1>&2 "[start addr2func]"
  cat ./iftracer-tmp.out | addr2func "$elf_filepath" $(cat ./iftracer-tmp.out | grep -E -o '0[xX][0-9a-fA-F]+' | tr -d '[]' | sort | uniq | tr '\n' ' ') >./iftracer-human.out

  echo 1>&2 "[start convert to trace json]"
  {
    cat <<EOF
{
  "traceEvents": [
EOF
    local i=0
    # -r: Backslash  does not act as an escape character.  The backslash is considered to be part of the line. In particular, a backslash-newline pair can not be used as a line continuation.
    while IFS= read -r line || [[ -n "$line" ]]; do
      local tid=$(echo $line | cut -d' ' -f1)
      local ts=$(echo $line | cut -d' ' -f2)
      local flag=$(echo $line | cut -d' ' -f3)
      local caller=$(echo $line | cut -d' ' -f4)
      local callee=$(echo $line | cut -d' ' -f5)
      local callee_func=$(echo $callee | cut -d':' -f1 | tr -d '<>' | c++filt)
      local callee_file=$(echo $callee | cut -d':' -f2-)
      local ph="B"
      if [[ $flag == "exit" ]]; then
        ph="E"
      fi
      if [[ $i != 0 ]]; then
        echo ','
      fi
      cat <<EOF
    {
      "name": "$callee_func",
      "ph": "$ph",
      "ts": $ts,
      "pid": 1234,
      "tid": $tid,
      "args": {
        "file":"$callee_file"
      }
    }
EOF
      ((i++))
    done < <(cat ./iftracer-human.out)

    cat <<EOF
  ],
  "displayTimeUnit": "ns",
  "systemTraceEvents": "SystemTraceData",
  "otherData": {
    "version": "My Application v1.0"
  }
}
EOF
  } >"$output_file"
  echo 1>&2 "[output]: $output_file"
}

main "$@"

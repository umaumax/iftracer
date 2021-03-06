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
  if [[ "${#output[@]}" == "0" ]]; then
    echo 1>&2 "objdump filter ends with 0 result"
    return 1
  fi
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
      echo 1>&2 "[set -offset-1 mode]"
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
    # hex addr 0 offset
    cat ./iftracer.out.* >./iftracer-tmp.out
  else
    # hex addr -1 offset for arm thumb mode
    cat ./iftracer.out.* | sed -E -e 's/1$/0/' -e 's/5$/4/' -e 's/9$/8/' -e 's/d$/c/' >./iftracer-tmp.out
  fi

  if [[ ! -f "$elf_filepath" ]]; then
    echo 1>&2 "not found $elf_filepath"
    return 1
  fi

  echo 1>&2 "[start addr2func]"
  cat ./iftracer-tmp.out | addr2func "$elf_filepath" $(cat ./iftracer-tmp.out | grep -E -o '0[xX][0-9a-fA-F]+' | tr -d '[]' | sort | uniq | tr '\n' ' ') >./iftracer-human.out

  # Is this empty file?
  if [[ ! -s ./iftracer-human.out ]]; then
    echo 1>&2 "Maybe you should add -offset-1 option"
    help
    return 1
  fi

  echo 1>&2 "[start convert to trace json]"
  {
    echo -n "
{
  \"traceEvents\": [
"
    local i=0
    # -r: Backslash  does not act as an escape character.  The backslash is considered to be part of the line. In particular, a backslash-newline pair can not be used as a line continuation.
    while IFS= read -r line || [[ -n "$line" ]]; do
      local fields=($line)
      local tid=${fields[0]}
      local ts=${fields[1]}
      local flag=${fields[2]}
      local caller=${fields[3]}
      local callee=${fields[4]}
      local callee_func=$(echo $callee | cut -d':' -f1 | tr -d '<>')
      local callee_file=$(echo $callee | cut -d':' -f2-)
      local ph="B"
      if [[ $flag == "exit" ]]; then
        ph="E"
      fi
      if [[ $i != 0 ]]; then
        echo ','
      fi
      echo -n "
    {
      \"name\": \"$callee_func\",
      \"ph\": \"$ph\",
      \"ts\": $ts,
      \"pid\": 1234,
      \"tid\": $tid,
      \"args\": {
        \"file\":\"$callee_file\"
      }
    }
"
      ((i++))
    done < <(cat ./iftracer-human.out)

    echo -n "
  ],
  \"displayTimeUnit\": \"ns\",
  \"systemTraceEvents\": \"SystemTraceData\",
  \"otherData\": {
    \"version\": \"My Application v1.0\"
  }
}
"
  } | c++filt >"$output_file"
  echo 1>&2 "[output]: $output_file"
}

main "$@"

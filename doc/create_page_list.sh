#!/bin/bash

# This Bash snippet is used to automatically generate an alphabetical list included by index.adoc
# It's invoked by at least 2 callers:
# - the Makefile[.am] for automake builds
# - the ReadTheDocs pipeline, see .readthedocs.yaml

OUTPUT_FILE="$1"
ASCIIDOC_DIR="$2"

echo >$OUTPUT_FILE
for adocfile in $(ls ${ASCIIDOC_DIR}/*.txt); do
    adocfile_basename=$(basename ${adocfile})

    # this script is used to produce an Asciidoc snippet that goes inside index.adoc... avoid listing index.adoc itself!
    if [ "${adocfile_basename}" != "index.txt" ]; then
        adocfile_basename_noext=${adocfile_basename//.txt/}
        echo "* link:${adocfile_basename_noext}.html[${adocfile_basename_noext}]" >>$OUTPUT_FILE
    fi
done
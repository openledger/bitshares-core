#!/bin/bash
sed -i -r '/(_index|\"_id|_score|_type)/d' ${1}

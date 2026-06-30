#!/bin/bash

rsync -e "ssh -i ~/.ssh/edge_up_key" -avz --delete --partial --progress \
  --exclude='.git' --exclude='node_modules' --exclude='__pycache__' --exclude='build' \
  . apons@edge-up-3.mmwunibo.it:~/fork-tesi-uafx/

rsync -e "ssh -i ~/.ssh/edge_up_key" -avz --delete --partial --progress \
  --exclude='.git' --exclude='node_modules' --exclude '__pycache__' --exclude='build' \
  . apons@edge-up-4.mmwunibo.it:~/fork-tesi-uafx/

rsync -e "ssh -i ~/.ssh/edge_up_key" -avz --delete --partial --progress \
  --exclude='.git' --exclude='node_modules' --exclude '__pycache__' --exclude='build' \
  . apons@apons-radius.mmwunibo.it:~/fork-tesi-uafx/

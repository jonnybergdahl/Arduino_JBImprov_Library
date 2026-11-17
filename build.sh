#!/usr/bin/env zsh
# Build the Doxygen docs and publish them to jonnybergdahl.github.io/jbimprov.

if [ ! -f doxygen-awesome-css/doxygen-awesome.css ]; then
  echo "doxygen-awesome-css is missing. Run: git submodule update --init"
  exit 1
fi

start_path=$(pwd)
/opt/homebrew/bin/doxygen Doxyfile || exit 1
mkdir -p ~/Developer/jonnybergdahl/jonnybergdahl.github.io/jbimprov
rm -rf ~/Developer/jonnybergdahl/jonnybergdahl.github.io/jbimprov/*
cp -r docs/* ~/Developer/jonnybergdahl/jonnybergdahl.github.io/jbimprov

# Commit and push docs
cd ~/Developer/jonnybergdahl/jonnybergdahl.github.io || exit
git checkout main
git add jbimprov
if [ -n "$(git status --porcelain jbimprov)" ]; then
  git commit -m "Update JBImprov docs"
  git push
else
   echo "Branch main is up to date,nothing to do."
fi
cd "$start_path" || exit

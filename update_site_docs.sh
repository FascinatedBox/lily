# This script handles the job of updating the site documentation in the gh-pages
# branch. This script isn't designated as a git hook, because it switches
# branches so that it can create a branch in another area.

# This script has a couple assumptions:
# * You've just committed something that updates the docs. Don't run this during
#   a commit that updates the markdown files. Only after.
# * You are on master, and you have the gh-pages branch cloned.

mkdir -p html_cache

cd site

# This script will use .markdown files to generate the html files. These files
# are stored in the html_cache that was created above. That directory is
# intentionally not in source control.
python generate_site.py

cd ..

git checkout gh-pages

mv html_cache/* .

rm -r html_cache

git commit -a -m "Syncing documentation"

git checkout master

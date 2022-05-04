# Cinder JIT IR Viewer

This directory contains a graphical viewer for Cinder JIT IRs. Currently only
HIR is supported. Support for the new LIR should be added soon.

## Developing

You'll need `nodejs` and `npm` to develop locally. It's strongly recommended
that you develop locally, instead of on your devserver. If you don't have the
cinder repo checked out locally, you can run `fbclone cinder` to do so. If
you're on a Mac and use Homebrew, these can be installed easily using `brew install nodejs npm`.

As a prerequisite, you'll need to install development dependences using `npm install`.

**Writing and running unit tests**

- We use `Jest` as our unit test framework.
- Test files should live in the same directory as the code that they test and
  have the suffix `.test.js`.
- You can run the test suite with `npm run test`.

**Viewing your changes**

We use `webpack` to compile the app into a single bundle in the `dist`
directory. To view your local changes:

- Run `npx webpack`
- Open `dist/index.html` in your browser
- Copy the `dist` directory to `/mnt/homedir/<username>/public_html/irviewer` and visit `https://home.fburl.com/~<username>/irviewer/` in your browser.

**Submitting your changes for review**

Run `bin/presubmit` before submitting your changes for review. It will run unit tests
and format your code.

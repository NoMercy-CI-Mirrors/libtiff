Contributing
============

AI policy
---------

The use of AI tools and services is permitted, but you are fully
responsible for anything you submit. You are expected to have read,
understood, reviewed and tested all AI-generated code and documentation
in your submission, and that you have the right to contribute it under
the libtiff license. Please disclose the AI tool and model used (in an
`Assisted-by:` commit trailer, the merge request description, or at the
end of issue text). Please read the
[AI policy](doc/sphinx/project/ai-policy.rst) before using AI tools to prepare
a merge request, issue or security report.

API documentation
-----------------

Library API and implementation documentation uses Doxygen. See
[the documentation guide](doc/doxygen/guide.md) for comment conventions,
module organisation, build instructions, and downstream cross-references.
The existing Sphinx manual and man pages remain maintained alongside it.
CI fails on missing or invalid Doxygen markup; to check locally, configure
with `-Ddoxygen=ON -Ddoxygen-checks=ON` and build the
`tiff-doc-doxygen-check` target.

Code formatting
---------------

libtiff uses a ``.clang-format`` file to enforce code formatting rules.

Automatic code reformatting can be done with pre-commit.

Install (once) pre-commit with ``python -m pip install pre-commit``.

Install it (once) in the libtiff git repository with ``pre-commit install``.

Then the rules defined in the ``.pre-commit-config.yaml`` file will be
enforced at ``git commit`` time, with automatic reformatting.

Due to whole-tree code reformatting done during libtiff 4.5 development,
``git blame`` information might be misleading. To avoid that, you need
to modify your git configuration as following to ignore the revision of
the whole-tree reformatting:
``git config blame.ignoreRevsFile .git-blame-ignore-revs``.

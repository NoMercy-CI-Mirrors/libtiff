AI Policy
=========

The LibTIFF project permits the responsible use of AI tools and services.
Contributors are welcome to use AI assistants, code generators, review tools
and similar services to help them work more effectively, subject to the
guidelines below.

The underlying principle is simple: **the human submitter is fully responsible
for everything they submit**, regardless of how it was produced.  Using an AI
tool does not lessen that responsibility, and "the AI wrote it" is not an
acceptable explanation for a defect, a regression or a misleading report.

Responsibility
--------------

* A human author must be responsible for every submission to the LibTIFF
  project.  This includes merge requests, commits, issues, security reports,
  review comments and posts to the :doc:`mailing list <mailinglist>`.
  AI tools and services must not open merge requests or issues, or post
  comments, without a human author who is associated with and accountable
  for them.

* The submitter is expected to have **read, understood, reviewed and tested**
  all AI-generated or AI-assisted code, tests and documentation before
  submitting it.  You should be able to explain any part of your change, and
  respond to review feedback on it, without deferring to an AI tool.

* Code changes must be built and tested by the submitter, including adding or
  updating tests in :file:`test/` where appropriate.  Documentation changes
  should be checked for accuracy against the actual behaviour of the library
  and tools.

Authorship
----------

* AI tools and services are not considered authors.  They should not be
  listed as authors or co-authors of commits (for example, with
  ``Co-authored-by:`` trailers), and the human submitter should be the
  commit author.

* Review by an AI tool is not a substitute for the submitter's own review,
  and is not considered adequate evidence of the submitter's understanding or
  of the correctness of a proposed change.

Disclosure
----------

If AI tools made a material contribution to a submission, please say so,
naming the tool and the model used (including its version, where known).
This helps reviewers to judge the submission and lets the project trace the
provenance of its code.  Routine use such as editor autocompletion of
trivial code, or spelling and grammar checking, need not be disclosed.

* **Issues and security reports:** add a note at the end of the report,
  for example::

      This report was prepared with the assistance of <tool> (<model>).

* **Merge requests:** describe in the merge request description which tool
  and model were used, and what they were used for (for example, generating
  an initial implementation, writing tests, or editing documentation).

* **Commits:** add an ``Assisted-by:`` trailer to the commit message for each
  tool used, for example::

      Assisted-by: <tool> (<model>)

  This records the assistance without listing the tool as an author.

Copyright and licensing
-----------------------

LibTIFF must remain free of encumbered code.  AI models are trained on large
amounts of existing material, much of it copyrighted, and may reproduce it
closely or verbatim.  The legal status of AI-generated output is also not yet
settled.  Submitters must therefore take particular care:

* By submitting a contribution, the submitter confirms that they have the
  right to contribute it under the LibTIFF :doc:`license <license>`, as for
  any other contribution.

* The submitter must only use AI tools whose terms of use permit the output
  to be contributed under the LibTIFF license, and which do not claim
  ownership of, or place restrictions on the use or distribution of, the
  output.

* The submitter must not submit AI output which reproduces, or is closely
  derived from, existing code or text which is not compatible with the
  LibTIFF license.  Output that resembles code from another project, or that
  is unusually large or polished for the prompt given, should be treated
  with suspicion.

* Where the provenance or licensing of a contribution is uncertain, the
  maintainers may decline to accept it.

Bug and security reports
------------------------

* Issues and security reports, including those based on the output of
  AI-driven analysis or fuzzing tools, must be verified by the submitter
  before they are filed.  Please include a minimal reproducer (for example,
  a small TIFF file and the command or code that triggers the problem) and
  confirm that the problem reproduces with the current development sources.
  See :doc:`bugs` for further details.

* Unverified, speculative or bulk-generated reports consume a large amount of
  volunteer maintainer time.  Such reports may be closed without further
  investigation.

Maintainer discretion
---------------------

LibTIFF is maintained by a small group of volunteers.  The maintainers may
decline or close any submission which does not appear to meet the
expectations of this policy, for example where the submitter is unable to
explain or justify the change, or where the change has evidently not been
built or tested.

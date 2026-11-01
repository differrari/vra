(load "~/redbuild/packaging/packager.lisp")
(load "~/redbuild/v3/redbuild.lisp")

(redbuild:make "~/diylib" "-j")

(redbuild:set-global-target (redbuild:native))

(redbuild:build-dep "~/uno")

(defvar pname "vra")

(defun pkginstall (pname pack) 
    (if (or (eq (redbuild:dyn-target) :redacted) *should_package*)
        (generate-build pack (redbuild:dyn-target))
    )
    (if (eq (redbuild:dyn-target) :redacted)
        (progn
            (redbuild:install (concatenate `string pname ".red"))
            (redbuild:make "~/os" "run")
        )
    )
)

(redbuild:quick-build (redbuild:make-instance `redbuild:redmod
    :name pname
    :type :bin
    :libs (list (redbuild:local-lib "uno" :lib "uno.a"))
    :target (redbuild:dyn-target)
    :srcs (redbuild:all-sources "c")
) :add-dependencies t :run (not (eq (redbuild:dyn-target) :redacted)) :success 
    (lambda () 
        (print (redbuild:emit-compile-commands))
        (pkginstall pname (make-instance `pack
            :name pname
            :version "0.1.0a"
            :author "di"
            :exec pname
            :categories "Graphics"
            :id (concatenate `string "com.differrari." pname)
        ))
    )
)
(load "~/redbuild/packaging/packager.lisp")
(load "~/redbuild/v3/redbuild.lisp")

(redbuild:set-global-target :redacted)

(redbuild:rebuild-sharedlib)

(redbuild:build-dep "~/uno")

(defvar pname "vra")

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
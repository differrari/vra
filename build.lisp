(load "~/redbuild/v3/redbuild.lisp")

(redbuild:make "~/diylib" "-j")

; (redbuild:build-dep "~/...")

(redbuild:quick-build (redbuild:make-instance `redbuild:redmod
    :name "vra"
    :type :bin
    ; :libs (list (redbuild:local-lib "dep" :lib "dep.a"))
    :target (redbuild:native)
    :srcs (redbuild:all-sources "c")
) :add-dependencies t :run t :success (lambda () (print "Done") (print (redbuild:emit-compile-commands))))
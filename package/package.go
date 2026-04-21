package cmui

import (
	denv "github.com/jurgen-kluft/ccode/denv"
	ccore "github.com/jurgen-kluft/ccore/package"
	cgx2 "github.com/jurgen-kluft/cgx2/package"
	cunittest "github.com/jurgen-kluft/cunittest/package"
)

const (
	repo_path = "github.com\\jurgen-kluft\\"
	repo_name = "cmui"
)

func GetPackage() *denv.Package {
	name := repo_name

	// dependencies
	cunittestpkg := cunittest.GetPackage()
	ccorepkg := ccore.GetPackage()
	cgx2pkg := cgx2.GetPackage()

	// main package
	mainpkg := denv.NewPackage(repo_path, repo_name)
	mainpkg.AddPackage(cunittestpkg)
	mainpkg.AddPackage(ccorepkg)
	mainpkg.AddPackage(cgx2pkg)

	// main library
	mainlib := denv.SetupCppLibProject(mainpkg, name)
	mainlib.AddDependencies(ccorepkg.GetMainLib())
	mainlib.AddDependencies(cgx2pkg.GetMainLib())

	// test library
	testlib := denv.SetupCppTestLibProject(mainpkg, name)
	testlib.AddDependencies(ccorepkg.GetTestLib())
	testlib.AddDependencies(cunittestpkg.GetTestLib())
	testlib.AddDependencies(cgx2pkg.GetTestLib())

	// unittest project
	maintest := denv.SetupCppTestProject(mainpkg, name)
	maintest.AddDependencies(cunittestpkg.GetMainLib())
	maintest.AddDependency(testlib)

	mainpkg.AddMainLib(mainlib)
	mainpkg.AddTestLib(testlib)
	mainpkg.AddUnittest(maintest)
	return mainpkg
}

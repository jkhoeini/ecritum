package ecritum;

import java.util.List;

interface StandardLibraryBridge {
    Object invoke(String operation, List<Object> arguments);

    static StandardLibraryBridge denying() {
        return (operation, arguments) -> {
            throw StandardLibraryException.permissionDenied(operation + " is not permitted");
        };
    }
}

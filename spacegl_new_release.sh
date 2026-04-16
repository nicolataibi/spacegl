#!/bin/bash
# ==============================================================================
# SPACE GL - Professional Release Script (Version++)
# ==============================================================================

set -euo pipefail

GIT_ROOT="../spacegl"
SPEC_FILE="spacegl.spec"
CHANGELOG_FILE="changelog"
TODAY=$(date +%Y.%m.%d)

# Funzioni di Logging (Formato Standardizzato e ANSI Colors se supportato)
log_info()    { echo -e "[\033[34m ℹ \033[0m] $1"; }
log_success() { echo -e "[\033[32m ✔ \033[0m] $1"; }
log_warn()    { echo -e "[\033[33m ⚠ \033[0m] $1"; }
log_error()   { echo -e "[\033[31m ✘ \033[0m] $1" >&2; }

# Funzione per cleanup e rollback in caso di errore
cleanup_and_rollback() {
    local exit_code=$?
    log_error "Operazione fallita (Exit Code: $exit_code). Avvio rollback..."
    
    pushd "$GIT_ROOT" > /dev/null
        if [ -f "${SPEC_FILE}.bak" ]; then
            mv "${SPEC_FILE}.bak" "$SPEC_FILE" 2>/dev/null || true
        fi
        rm -f ".tmp_changelog"
        
        # Eliminazione di tag rimasti in attesa di push nel contesto del crash
        git tag --points-at HEAD | xargs -r git tag -d >/dev/null 2>&1 || true
        
        # Esegue reset solo per commit orfani o scollati non inseriti in origin/main
        local unpushed_commits
        unpushed_commits=$(git log origin/main..HEAD --oneline 2>/dev/null | wc -l || echo 0)
        if [ "$unpushed_commits" -gt 0 ]; then
            log_warn "Rilevati $unpushed_commits commit locali scartati. Ripristino a origin/main..."
            git reset --hard origin/main >/dev/null 2>&1 || true
        fi
    popd > /dev/null
    exit "$exit_code"
}
trap cleanup_and_rollback ERR

log_info "Starting Full Release Process..."

# 1. Check Changelog
TODAY_DATE=$(LC_ALL=en_US.UTF-8 date +"%a %b %d %Y")
if ! grep -q "$TODAY_DATE" "$CHANGELOG_FILE"; then
    log_warn "Il Changelog non contiene una voce per oggi ($TODAY_DATE). Continuare? (y/N)"
    read -r -p "> " confirm
    [[ "${confirm,,}" != "y" ]] && exit 1
fi

# 2. Increment Version
CURRENT_VERSION=$(grep "^Version:" "$SPEC_FILE" | awk '{print $2}')
NEW_VERSION="$TODAY.01"

if [[ "$CURRENT_VERSION" == "$TODAY"* ]]; then
    LAST_DIGIT=$(echo "$CURRENT_VERSION" | awk -F. '{print $NF}')
    NEXT_DIGIT=$(printf "%02d" $((10#$LAST_DIGIT + 1)))
    NEW_VERSION="${TODAY}.${NEXT_DIGIT}"
fi

log_info "Upgrading version: $CURRENT_VERSION -> $NEW_VERSION"
sed -i "s/^Version:.*/Version:        $NEW_VERSION/" "$SPEC_FILE"
# Garanzia di azzeramento macro in caso il dev environment sia impuro
sed -i "s/^Release:.*/Release:        %autorelease/" "$SPEC_FILE"

# 3. Synchronize Workspace tramite Rsync
log_info "Syncing workspace to $GIT_ROOT via rsync..."
if ! command -v rsync &>/dev/null; then
    log_error "Comando 'rsync' mancante. Impossibile sincronizzare."
    exit 1
fi
rsync -av --exclude='.git' --exclude='build' --delete ./ "$GIT_ROOT/" >/dev/null

# 4. Commit, Tagging, Build & Deploy
pushd "$GIT_ROOT" > /dev/null
    git add .
    DESC=$(grep "^-" "changelog" | head -n 1 | sed 's/^- //')
    
    log_info "Committing release updates..."
    git commit -m "[release] $NEW_VERSION: $DESC" >/dev/null
    
    log_info "Tagging local release $NEW_VERSION..."
    git tag -a "$NEW_VERSION" -m "Release $NEW_VERSION"
    
    log_info "Building SRPM per validazione indipendente..."
    git archive --format=tar.gz --prefix="spacegl-${NEW_VERSION}/" "HEAD" -o "spacegl-${NEW_VERSION}.tar.gz"
    mv "spacegl-${NEW_VERSION}.tar.gz" ~/rpmbuild/SOURCES/
    
    # Appiattimento del file SPEC limitatamente alla generazione autonoma.
    cp "$SPEC_FILE" "${SPEC_FILE}.bak"
    CALC_REL=$(rpmautospec calculate-release "$SPEC_FILE" | grep "Calculated release number" | sed 's/.*: //')
    sed -i "s/%autorelease/$CALC_REL%{?dist}/" "$SPEC_FILE"
    rpmautospec generate-changelog > .tmp_changelog
    sed -i '/%autochangelog/r .tmp_changelog' "$SPEC_FILE"
    sed -i '/%autochangelog/d' "$SPEC_FILE"
    
    rpmbuild --quiet -bs "$SPEC_FILE" --define "_sourcedir $HOME/rpmbuild/SOURCES"
    
    # Ripristino dello SPEC originale destinato a Copr
    mv "${SPEC_FILE}.bak" "$SPEC_FILE"
    rm -f .tmp_changelog "$HOME/rpmbuild/SOURCES/spacegl-${NEW_VERSION}.tar.gz"

    log_info "Syncing remote repository..."
    git push origin main
    git push origin "$NEW_VERSION"
popd > /dev/null

# Disabilita il trap
trap - ERR
log_success "Release $NEW_VERSION complete. Build in ~/rpmbuild/SRPMS/"
